////////////////////////////////////////
// assert.h
////////////////////////////////////////

#ifndef CORE_ASSERT_H
#define CORE_ASSERT_H

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#if __DEV

// __ASSERT: 1 when Assert() evaluates its expression (game code guards
// assert-only locals with `#if __ASSERT`).
#ifndef __ASSERT
#define __ASSERT 1
#endif

// Original RB assert semantics: asserts are NON-FATAL diagnostics.  The 2002
// build popped an Abort/Retry/Ignore dialog and engineers routinely continued;
// only crashes and Quitf halted execution.  Here the first failure per call
// site prints its message with file:line and execution continues; repeats at
// the same site are suppressed ("Ignore Always").  Set AGE_ASSERT_FATAL=1 in
// the environment to abort on the first failure instead (first-chance
// debugging under a debugger).
//
// Inline (not a .cpp) so the standalone unit testers, which don't link the
// engine lib, still resolve it; C++17 inline gives one shared instance.
inline bool ageAssertFail(const char *file, int line, const char *fmt, ...) {
    // -1 = not read yet.  AGE_ASSERT_FATAL=1 restores abort-on-assert.
    static int sFatal = -1;
    if (sFatal < 0) {
        const char *env = getenv("AGE_ASSERT_FATAL");
        sFatal = (env && env[0] && env[0] != '0') ? 1 : 0;
    }

    // "Ignore Always": one report per call site, keyed by the __FILE__
    // literal's address + line.  Overflowing sites keep printing (better
    // noisy than silent).
    struct Site {
        const char *File;
        int Line;
    };
    static Site sSeen[256];
    static int sSeenCount = 0;

    for (int i = 0; i < sSeenCount; i++) {
        if (sSeen[i].File == file && sSeen[i].Line == line) {
            return false;
        }
    }
    if (sSeenCount < 256) {
        sSeen[sSeenCount].File = file;
        sSeen[sSeenCount].Line = line;
        sSeenCount++;
    }

    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    fprintf(stderr, "ASSERT (ignored, once per site): %s  (%s:%d)\n", buf, file, line);
    fflush(stderr);

    if (sFatal) {
        abort();
    }
    return false;
}

#define Assert(x)        ((void)(!(x) ? (ageAssertFail(__FILE__, __LINE__, "%s", #x), 0) : 0))
#define AssertMsg(x, m)  ((void)(!(x) ? (ageAssertFail(__FILE__, __LINE__, "%s", (const char *)(m)), 0) : 0))
#define AssertQuitf(x, ...)    ((void)(!(x) ? (ageAssertFail(__FILE__, __LINE__, __VA_ARGS__), 0) : 0))
#define AssertErrorf(x, ...)   ((void)(!(x) ? (ageAssertFail(__FILE__, __LINE__, __VA_ARGS__), 0) : 0))
#define AssertWarningf(x, ...) ((void)(!(x) ? (ageAssertFail(__FILE__, __LINE__, __VA_ARGS__), 0) : 0))
#define AssertVerify(x) Assert(x)
#define DebugAssert(x)  Assert(x)
// Parameter / statement that exists only for an Assert.
#define ASSERT_ONLY(x) x

#else
#ifndef __ASSERT
#define __ASSERT 0
#endif
#define Assert(x)		((void)0)
#define AssertMsg(x,m)	((void)0)
#define AssertQuitf(x, ...) ((void)0)
#define AssertWarningf(x, ...) ((void)0)
#define AssertErrorf(x, ...) ((void)0)
#define AssertVerify(x) (x)
#define ASSERT_ONLY(x)
#endif

#define ASST_EXPR(x) (x)
#define Printf			printf

#endif // CORE_ASSERT_H
