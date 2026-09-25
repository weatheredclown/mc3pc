#include "memory/allocator.h"

#include "core/output.h"
#include "data/memory.h"

memMemoryAllocator *memMemoryAllocator::sm_Current = 0;

memMemoryAllocator::memMemoryAllocator()
{
	m_Base = 0;
	m_Size = 0;
	m_Static = false;
	m_Smallocator = true;
	m_LowWaterMark = 0;
	m_BaseUsed = 0;
}

void memMemoryAllocator::Init(void *base, unsigned size, bool isStatic)
{
	m_Base = base;
	m_Size = size;
	m_Static = isStatic;
	m_BaseUsed = (unsigned)datMemoryUsed();
	m_LowWaterMark = 0;
	if (!sm_Current) sm_Current = this;
}

void memMemoryAllocator::Kill()
{
	if (sm_Current == this) sm_Current = 0;
	m_Base = 0;
	m_Size = 0;
}

unsigned memMemoryAllocator::Used() const
{
	// A static (scratch) heap never hands out tracked blocks on the PC; the
	// main heap reports what the tagged allocator has seen since Init().
	if (m_Static) return 0;
	unsigned used = (unsigned)datMemoryUsed();
	return used > m_BaseUsed ? used - m_BaseUsed : 0;
}

unsigned memMemoryAllocator::Available() const
{
	unsigned used = Used();
	return used < m_Size ? m_Size - used : 0;
}

int memMemoryAllocator::GetMemUsed(int bucket) const
{
	return datMemoryUsed(bucket);
}

void memMemoryAllocator::GetStats(memMemStats *stats, bool) const
{
	if (!stats) return;
	stats->cbTotal = (int)m_Size;
	stats->cbUsed = (int)Used();
	stats->cbFree = (int)Available();
	stats->cbLargest = stats->cbFree;
	stats->nAllocs = datMemoryNumAllocs();
}

void memMemoryAllocator::Summary() const
{
	memMemStats s;
	GetStats(&s);
	Displayf("heap: %u total, %u used, %u free, %d allocs", s.cbTotal, s.cbUsed, s.cbFree, s.nAllocs);
}

bool memMemoryAllocator::CheckAbove(void *mark) const
{
	// The CRT heap has no address ordering to check; report clean.
	(void)mark;
	return true;
}

void memMemoryAllocator::LogAllocations()
{
	datMemoryStartTracking("LogAllocations");
}

#include <stdio.h>

void memMemoryAllocator::Summary(const char *filename) const
{
	Summary();
	if (!filename) return;
	FILE *f = fopen(filename, "w");
	if (!f) return;
	memMemStats st;
	GetStats(&st);
	fprintf(f, "used %d available %d\n", (int)Used(), (int)Available());
	fclose(f);
}
