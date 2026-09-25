////////////////////////////////////////
// memstats.h
//
// Load-time memory logging: nested sections that report how many bytes
// each stage of loading kept.  Silent unless -memlog is on the command
// line (the readings come from the data/memory.h tagged allocator).
////////////////////////////////////////

#ifndef DATA_MEMSTATS_H
#define DATA_MEMSTATS_H

// Print the allocator totals with a label (layer load milestones).
void datDisplayUsed(const char *label);
// Flush the memory log file (the game calls this at layer boundaries so a
// crash still leaves a complete log on disk).
void LogMemoryFlush();

#include "core/output.h"
#include "data/hash.h"

void LogMemoryStartSection(const char *fmt, ...);
void LogMemoryEndSection(const char *fmt, ...);
void LogMemoryMessage(const char *fmt, ...);
void LogMemory(const char *fmt, ...);
// Open the logs.  `fn` is where the game wants the log written; it passes the
// console host paths ("z:\\memstats.txt"), which do not exist on PC, so a name
// that cannot be opened falls back to the console output alone.
void LogMemoryOpen(const char *fn = 0);
void LogLoadOpen(const char *fn = 0);
void LogMemoryTotal(const char *fmt, ...);
void LogMemoryClose();
void LogLoadClose();
void LogMemoryPush();
void LogMemoryPop();

class rbHitTypeManager;
#ifdef HITTYPEMGR
#undef HITTYPEMGR
#endif
#define HITTYPEMGR (*rbHitTypeManager::smInstance)

#endif // DATA_MEMSTATS_H
