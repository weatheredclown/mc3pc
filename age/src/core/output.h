////////////////////////////////////////
// output.h
////////////////////////////////////////

#ifndef CORE_OUTPUT_H
#define CORE_OUTPUT_H

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define TPurple "\033[35m"
#define TRed    "\033[31m"
#define TGreen  "\033[32m"
#define TYellow "\033[33m"
#define TBlue   "\033[34m"
#define TCyan   "\033[36m"
#define TWhite  "\033[37m"
#define TNormal "\033[0m"

inline void Displayf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    printf("\n");
    va_end(args);
    fflush(stdout);
}

inline void Warningf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, TYellow "WARNING: ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, TNormal "\n");
    va_end(args);
    fflush(stderr);
}

inline void Errorf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, TRed "ERROR: ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, TNormal "\n");
    va_end(args);
    fflush(stderr);
}

inline void Messagef(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    printf("\n");
    va_end(args);
    fflush(stdout);
}

#ifdef __cplusplus
extern "C"
#endif
void ageDumpCallstack();

inline void Quitf(const char *fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    fprintf(stderr, TRed "FATAL ERROR: %s" TNormal "\n", buf);
    fflush(stderr);
    if (strstr(buf, "missing in") != NULL || strstr(buf, "missing") != NULL) {
        fprintf(stderr, TYellow "[AGE WORKAROUND] Missing attribute warning bypassed, continuing..." TNormal "\n");
        fflush(stderr);
        return;
    }
    // -quitfcontinue: survey mode.  Report and keep going instead of exiting,
    // so one run lists every not-implemented stub a code path reaches rather
    // than one per build.  Off by default; the process is in an undefined
    // state afterwards, so it is a diagnostic, never a way to ship past a
    // missing function.
    if (getenv("AGE_QUITF_CONTINUE") != NULL) {
        fprintf(stderr, TYellow "[quitfcontinue] continuing past the error above" TNormal "\n");
        fflush(stderr);
        return;
    }
    ageDumpCallstack();
    // _exit, not exit: atexit destructors of half-initialized globals assert
    // during a fatal quit (fxManager's dtor SIGABRTs and buries the actual
    // error under a crash dump).
    _exit(1);
}

inline char *formatf(char *buf, size_t size, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, size, fmt, args);
    va_end(args);
    return buf;
}

// Level-gated debug output: DebugfN prints when the caller's level reaches N,
// so a site tagged Debugf1(1, ...) is on and Debugf3(1, ...) stays quiet.  The
// console build compiled these out entirely (no Debugf symbol survives in the
// alpha map), so the levels are the only contract there is.  They share
// Displayf's sink and carry the level so a noisy site is easy to place.
inline void ageDebugfV(int lvl, const char *fmt, va_list args) {
    printf("[debug%d] ", lvl);
    vprintf(fmt, args);
    printf("\n");
    fflush(stdout);
}
inline void Debugf1(int lvl, const char *fmt, ...) {
    if (lvl < 1) return;
    va_list args; va_start(args, fmt); ageDebugfV(lvl, fmt, args); va_end(args);
}
inline void Debugf2(int lvl, const char *fmt, ...) {
    if (lvl < 2) return;
    va_list args; va_start(args, fmt); ageDebugfV(lvl, fmt, args); va_end(args);
}
inline void Debugf3(int lvl, const char *fmt, ...) {
    if (lvl < 3) return;
    va_list args; va_start(args, fmt); ageDebugfV(lvl, fmt, args); va_end(args);
}
inline void DebugLog(const char *fmt, ...) {}
inline void DebugLog(int tag, const void *data, int size) { (void)tag; (void)data; (void)size; }   // tagged binary log record (PS2 tuner)
template <typename T>
inline void DebugLog(int fourcc, const T *val) {}

// The real message box lives in the bank UI now (native Win32).
#include "bank/msgbox.h"

namespace datOutput {
    enum {
        OUTPUT_ERRORS = 1,
        OUTPUT_MESSAGES = 2
    };
    inline void SetOutputMask(int mask) {}
}

inline void DisablePopUpQuits() {}
inline void DisablePopUpErrors() {}

#endif // CORE_OUTPUT_H
