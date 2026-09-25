////////////////////////////////////////
// memory.cpp
//
// Tagged allocator for the PC port (see memory.h).  The original engine's
// OPNEW allocator prefixed headers; here a side hash table keyed on the
// pointer tracks {size, bucket, generation} for every operator new, so the
// returned pointers are plain malloc pointers and interop with era code is
// unchanged.
//
// Design notes:
//  - Zero-initialized statics only: operator new runs during static init,
//    before any constructor in this TU.  The table itself is lazily
//    malloc'd (raw CRT - malloc is NOT overridden, so no recursion).
//  - Open addressing with tombstones; the table is rebuilt in place when
//    tombstones accumulate.  Capacity 1M entries.
//  - A full table stops tracking new entries (counted); totals still grow
//    at alloc but can no longer shrink for untracked pointers, so the
//    numbers degrade pessimistically instead of corrupting.
//  - Thread-safe via a spinlock; the current bucket is thread-local.
////////////////////////////////////////

#include "data/memory.h"
#include "core/output.h"

bool datEnableMemoryFill = false;

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <new>

#if defined(_MSC_VER)
#include <intrin.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>  // HeapValidate (datMemoryValidatePtr)
#endif

namespace
{

// ---- lock ------------------------------------------------------------

volatile long sLock = 0;

inline void LockAcquire(void)
{
	while (_InterlockedExchange(&sLock, 1))
		_mm_pause();
}

inline void LockRelease(void)
{
	_InterlockedExchange(&sLock, 0);
}

struct ScopedLock
{
	ScopedLock()  { LockAcquire(); }
	~ScopedLock() { LockRelease(); }
};

// ---- current bucket (thread-local, zero-init = MEMBUCKET_DEFAULT) ----

__declspec(thread) int sCurrentBucket = 0;

// ---- tracking table --------------------------------------------------

enum
{
	TABLE_BITS = 20,
	TABLE_SIZE = 1 << TABLE_BITS,
	TABLE_MASK = TABLE_SIZE - 1,

	SLOT_EMPTY = 0,
	SLOT_LIVE = 1,
	SLOT_TOMB = 2
};

struct Entry
{
	void          *Ptr;
	unsigned       Size;
	unsigned       Gen;
	unsigned short Bucket;
	unsigned short State;
};

Entry    *sTable;                       // lazy
__int64   sBucketBytes[MEMBUCKET_MAX];
__int64   sTotalBytes;
int       sLive;
int       sTombs;
int       sUntracked;                   // allocs dropped because table full
unsigned  sGen = 0;                     // bumped by StartTracking

inline unsigned HashPtr(const void *p)
{
	// Pointers from malloc are >= 16-byte aligned.
	unsigned __int64 v = (unsigned __int64)p >> 4;
	v *= 0x9E3779B97F4A7C15ull;
	return (unsigned)(v >> 32);
}

bool EnsureTable(void)
{
	if (sTable)
		return true;
	sTable = (Entry*)calloc(TABLE_SIZE, sizeof(Entry));
	return sTable != 0;
}

void Rebuild(void)
{
	// Rehash live entries into a fresh table to shed tombstones.
	Entry *fresh = (Entry*)calloc(TABLE_SIZE, sizeof(Entry));
	if (!fresh)
	{	// Out of memory for bookkeeping - keep limping with tombstones.
		return;
	}
	for (int i = 0; i < TABLE_SIZE; i++)
	{
		if (sTable[i].State != SLOT_LIVE)
			continue;
		unsigned j = HashPtr(sTable[i].Ptr) & TABLE_MASK;
		while (fresh[j].State == SLOT_LIVE)
			j = (j + 1) & TABLE_MASK;
		fresh[j] = sTable[i];
	}
	free(sTable);
	sTable = fresh;
	sTombs = 0;
}

void TrackAlloc(void *p, size_t size)
{
	int bucket = sCurrentBucket;
	if (bucket < 0 || bucket >= MEMBUCKET_MAX)
		bucket = MEMBUCKET_DEFAULT;

	ScopedLock lock;

	sBucketBytes[bucket] += (__int64)size;
	sTotalBytes += (__int64)size;

	if (!EnsureTable())
	{	sUntracked++;
		return;
	}
	if (sLive >= (TABLE_SIZE * 3) / 4)
	{	// Table effectively full: stop tracking this allocation.  Its bytes
		// were counted above and will never be uncounted - a pessimistic
		// degradation, flagged via sUntracked.
		sUntracked++;
		return;
	}
	if (sTombs > TABLE_SIZE / 4)
		Rebuild();

	unsigned i = HashPtr(p) & TABLE_MASK;
	unsigned insertAt = (unsigned)-1;
	for (;;)
	{
		unsigned short state = sTable[i].State;
		if (state == SLOT_EMPTY)
		{	if (insertAt == (unsigned)-1)
				insertAt = i;
			break;
		}
		if (state == SLOT_TOMB)
		{	if (insertAt == (unsigned)-1)
				insertAt = i;
		}
		else if (sTable[i].Ptr == p)
		{	// Same pointer twice without a free: shouldn't happen (malloc
			// wouldn't return it) - treat as replace to stay consistent.
			insertAt = i;
			break;
		}
		i = (i + 1) & TABLE_MASK;
	}

	Entry &e = sTable[insertAt];
	if (e.State == SLOT_TOMB)
		sTombs--;
	e.Ptr = p;
	e.Size = (unsigned)size;
	e.Gen = sGen;
	e.Bucket = (unsigned short)bucket;
	e.State = SLOT_LIVE;
	sLive++;
}

void TrackFree(void *p)
{
	ScopedLock lock;

	if (!sTable)
		return;

	unsigned i = HashPtr(p) & TABLE_MASK;
	for (;;)
	{
		unsigned short state = sTable[i].State;
		if (state == SLOT_EMPTY)
			return;  // untracked pointer (pre-table, or dropped when full)
		if (state == SLOT_LIVE && sTable[i].Ptr == p)
			break;
		i = (i + 1) & TABLE_MASK;
	}

	Entry &e = sTable[i];
	sBucketBytes[e.Bucket] -= (__int64)e.Size;
	sTotalBytes -= (__int64)e.Size;
	e.State = SLOT_TOMB;
	e.Ptr = 0;
	sLive--;
	sTombs++;
}

void *DoAlloc(size_t size)
{
	if (size == 0)
		size = 1;
	void *p = malloc(size);
	if (!p)
		Quitf("Out of memory allocating %u bytes.", (unsigned)size);
	TrackAlloc(p, size);
	return p;
}

void DoFree(void *p)
{
	if (!p)
		return;
	TrackFree(p);
	free(p);
}

// ---- leak-tracking windows -------------------------------------------

enum { MAX_TRACK_DEPTH = 8 };

struct TrackFrame
{
	char     Name[64];
	unsigned Gen;
	__int64  BytesAtStart;
};

TrackFrame sTrackStack[MAX_TRACK_DEPTH];
int        sTrackDepth;

const char *sBucketNames[MEMBUCKET_MAX] =
{
	"default",
	"uniqtextures",
	"insttextures",
	"uniqgeometry",
	"instgeometry",
	"hash",
	"level",
	"ai",
	"textures",
	"geometry",
	"boundgeometry"
};

} // anonymous namespace

// ---- global operator new/delete --------------------------------------

void *operator new(size_t size)                            { return DoAlloc(size); }
void *operator new[](size_t size)                          { return DoAlloc(size); }
void *operator new(size_t size, const std::nothrow_t&) throw()   { return DoAlloc(size); }
void *operator new[](size_t size, const std::nothrow_t&) throw() { return DoAlloc(size); }

void operator delete(void *p) throw()                      { DoFree(p); }
void operator delete[](void *p) throw()                    { DoFree(p); }
void operator delete(void *p, size_t) throw()              { DoFree(p); }
void operator delete[](void *p, size_t) throw()            { DoFree(p); }
void operator delete(void *p, const std::nothrow_t&) throw()   { DoFree(p); }
void operator delete[](void *p, const std::nothrow_t&) throw() { DoFree(p); }

// ---- public API -------------------------------------------------------

int datMemoryUsed(int bucket)
{
	ScopedLock lock;
	if (bucket < 0)
		return (int)sTotalBytes;
	if (bucket >= MEMBUCKET_MAX)
		return 0;
	return (int)sBucketBytes[bucket];
}

int datMemoryUsed(const char *name)
{
	if (!name)
		return datMemoryUsed(-1);
	for (int i = 0; i < MEMBUCKET_MAX; i++)
		if (!_stricmp(name, sBucketNames[i]))
			return datMemoryUsed(i);
	return 0;
}

int datMemoryNumAllocs(void)
{
	ScopedLock lock;
	return sLive;
}

bool datMemoryValidatePtr(const void *p)
{
	if (!p)
		return false;
	return HeapValidate(GetProcessHeap(), 0, p) != 0;
}

datUseMemoryBucket::datUseMemoryBucket(int bucket)
{
	Prev = sCurrentBucket;
	if (bucket >= 0 && bucket < MEMBUCKET_MAX)
		sCurrentBucket = bucket;
}

datUseMemoryBucket::~datUseMemoryBucket()
{
	sCurrentBucket = Prev;
}

void datMemoryStartTracking(const char *name)
{
	ScopedLock lock;
	if (sTrackDepth >= MAX_TRACK_DEPTH)
		return;
	TrackFrame &f = sTrackStack[sTrackDepth++];
	sGen++;
	f.Gen = sGen;
	f.BytesAtStart = sTotalBytes;
	f.Name[0] = 0;
	if (name)
	{	strncpy(f.Name, name, sizeof(f.Name) - 1);
		f.Name[sizeof(f.Name) - 1] = 0;
	}
}

void datMemoryEndTracking(const char *name)
{
	// Gather under the lock, report outside it (Displayf may allocate).
	__int64 leakBucketBytes[MEMBUCKET_MAX];
	int leakBucketCounts[MEMBUCKET_MAX];
	__int64 leakTotal = 0;
	int leakCount = 0;
	char label[64];

	{
		ScopedLock lock;
		if (sTrackDepth <= 0)
			return;
		TrackFrame &f = sTrackStack[--sTrackDepth];
		strncpy(label, name ? name : f.Name, sizeof(label) - 1);
		label[sizeof(label) - 1] = 0;

		memset(leakBucketBytes, 0, sizeof(leakBucketBytes));
		memset(leakBucketCounts, 0, sizeof(leakBucketCounts));
		if (sTable)
		{
			for (int i = 0; i < TABLE_SIZE; i++)
			{
				const Entry &e = sTable[i];
				if (e.State == SLOT_LIVE && e.Gen >= f.Gen)
				{
					leakBucketBytes[e.Bucket] += e.Size;
					leakBucketCounts[e.Bucket]++;
					leakTotal += e.Size;
					leakCount++;
				}
			}
		}
	}

	if (leakCount == 0)
	{	Displayf("[memtrack] '%s': no live allocations from this window.",
				label);
		return;
	}
	Displayf("[memtrack] '%s': %d live allocations, %d bytes:",
			label, leakCount, (int)leakTotal);
	for (int b = 0; b < MEMBUCKET_MAX; b++)
		if (leakBucketCounts[b])
			Displayf("[memtrack]   %-14s %6d allocs  %9d bytes",
					sBucketNames[b], leakBucketCounts[b],
					(int)leakBucketBytes[b]);
	if (sUntracked)
		Displayf("[memtrack]   (+%d allocations untracked - table full)",
				sUntracked);
}
