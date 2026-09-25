////////////////////////////////////////
// memory.h
//
// Tagged allocator: global operator new/delete route through a tracking
// layer that attributes every live allocation to a memory bucket (see
// eMemoryBucket).  The PC port implements this with a side table keyed on
// the pointer - allocations are NOT prefixed with a header, so a stray
// free() of a new'd pointer stays exactly as (un)safe as plain CRT.
//
// datMemoryUsed(bucket) - bytes currently live in a bucket.
// datMemoryUsed()       - total live bytes from new/delete.
// datUseMemoryBucket    - scoped: allocations in scope tag to that bucket.
// datMemoryStartTracking/EndTracking - leak window; End reports what was
//   allocated since the matching Start and is still live.
////////////////////////////////////////

#ifndef DATA_MEMORY_H
#define DATA_MEMORY_H

#include "core/output.h"
#include "data/string.h"

int datMemoryUsed(int bucket = -1);
int datMemoryUsed(const char *name);
#define MEMUSED(x) datMemoryUsed(x)

inline void datMemoryStartUseTemporary() {}
inline void datMemoryEndUseTemporary() {}
void datMemoryStartTracking(const char *name = 0);
void datMemoryEndTracking(const char *name = 0);

// Number of live allocations currently tracked (diagnostics).
int datMemoryNumAllocs(void);

// Diagnostics: returns false if the pointer is not a valid live heap block
// (wrong start address, already freed, or its metadata is corrupt).
bool datMemoryValidatePtr(const void *p);

struct datUseMemoryBucket {
	datUseMemoryBucket(int bucket);
	~datUseMemoryBucket();
private:
	int Prev;
};

enum eMemoryBucket {
  MEMBUCKET_DEFAULT,
  MEMBUCKET_UNIQ_TEXTURES,
  MEMBUCKET_INST_TEXTURES,
  MEMBUCKET_UNIQ_GEOMETRY,
  MEMBUCKET_INST_GEOMETRY,
  MEMBUCKET_HASH,
  MEMBUCKET_LEVEL,
  MEMBUCKET_AI,
  MEMBUCKET_TEXTURES,
  MEMBUCKET_GEOMETRY,
  MEMBUCKET_BOUNDGEOMETRY,
  MEMBUCKET_MAX
};

extern bool datEnableMemoryFill;

#endif // DATA_MEMORY_H
