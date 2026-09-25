#ifndef MEMORY_ALLOCATOR_H
#define MEMORY_ALLOCATOR_H

////////////////////////////////////////
// memory/allocator.h
//
// memMemoryAllocator - the game heap object.  On the PC port allocations go
// through the CRT (data/memory.cpp tags them); this class carries the heap
// identity, its nominal size and the accounting the game code reads
// (Available/Used/Top, stats, low-water marks) so heap-blowout logic and the
// memory log keep working.
////////////////////////////////////////

struct memMemStats {
	int cbTotal;     // nominal heap size in bytes
	int cbUsed;      // bytes currently allocated
	int cbFree;      // cbTotal - cbUsed
	int cbLargest;   // largest free block (== cbFree on the CRT heap)
	int nAllocs;     // live allocation count
};

class memMemoryAllocator {
public:
	memMemoryAllocator();

	// Bind the allocator to a block of memory (a static heap) or, with a null
	// base, to a virtual heap of `size` bytes served by the CRT.
	void Init(void *base, unsigned size, bool isStatic = false);
	void Kill();

	static memMemoryAllocator *GetCurrent()				{ return sm_Current; }
	static void SetCurrent(memMemoryAllocator *alloc)	{ sm_Current = alloc; }

	unsigned Available() const;
	unsigned Used() const;
	unsigned Top() const								{ return m_Size; }
	void *GetBase() const								{ return m_Base; }
	int GetMemUsed(int bucket) const;

	void GetStats(memMemStats *stats, bool includeSmall = true) const;
	void Summary() const;
	void Summary(const char *filename) const;   // write the summary to a file (also echoed)
	bool CheckAbove(void *mark) const;
	void SetLowWaterMark(void *mark)					{ m_LowWaterMark = mark; }
	void *GetLowWaterMark() const						{ return m_LowWaterMark; }

	// The small-block allocator has no separate PC implementation; the flag
	// is honoured for reporting only.
	void DisableSmallocator()							{ m_Smallocator = false; }
	bool IsSmallocatorEnabled() const					{ return m_Smallocator; }

	static void LogAllocations();

private:
	static memMemoryAllocator *sm_Current;

	void *m_Base;
	unsigned m_Size;
	bool m_Static;
	bool m_Smallocator;
	void *m_LowWaterMark;
	unsigned m_BaseUsed;      // tracker usage when Init() ran
};

#endif // MEMORY_ALLOCATOR_H
