////////////////////////////////////////
// opnew.h
//
// Force-included on every translation unit (build passes /FI opnew.h /D OPNEW).
// In the shipping engine this installed the custom tagged allocator; during the
// D3D11 bootstrap we BYPASS that and use standard new/delete, and instead use
// this always-present header as the global prelude that makes the core
// fundamentals (sized types, NULL, Assert/log macros) available everywhere —
// matching the original code's assumption that they're just always in scope.
////////////////////////////////////////

#ifndef CORE_OPNEW_H
#define CORE_OPNEW_H

#include "core/types.h"
#include "core/assert.h"
#include "core/output.h"
#include "vector/amath.h"
#include "data/timemgr.h"
#include "data/colors.h"

#ifndef DECLARE_PLACE
#define DECLARE_PLACE(cls) \
    void* operator new(size_t, void* ptr) { return ptr; } \
    void* operator new(size_t sz) { return ::operator new(sz); } \
    void operator delete(void*, void*) {} \
    void operator delete(void* ptr) { ::operator delete(ptr); }
#endif

#ifndef IMPLEMENT_PLACE
#define IMPLEMENT_PLACE(cls)
#endif

#endif // CORE_OPNEW_H
