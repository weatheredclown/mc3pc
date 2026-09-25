#ifndef PROFILE_PAGE_H
#define PROFILE_PAGE_H

#include "profile/profiler.h"


// Profiler pages/groups/timers are compiled in (game code gates them on __STATS).
#ifndef __STATS
#define __STATS 1
#endif

#endif // PROFILE_PAGE_H
