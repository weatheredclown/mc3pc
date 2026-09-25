////////////////////////////////////////
// gputimer.h
//
// Per-pass GPU timing for the D3D11 backend.
//
// The race scene is GPU bound, so a CPU timer wrapped around a render pass
// measures how long it took to SUBMIT the pass, not how long the GPU spent on
// it - the two are unrelated once the context is a frame ahead.  This wraps
// D3D11 timestamp queries instead, so an fx pass can be costed for what it
// actually costs, and records the CPU submit cost of the same scope alongside
// it so a pass that is expensive on both is not mistaken for one that is
// expensive on neither.
//
// Off unless -gputime is on the command line; with it off, Push/Pop are a
// load, a compare and a return.
////////////////////////////////////////

#ifndef GFX_GPUTIMER_H
#define GFX_GPUTIMER_H

#if __WIN32PC

// True once -gputime has been seen (the first frame reads the args).
bool gfxGpuTimerEnabled();

// Frame bracket, driven from gfxBeginFrame / gfxEndFrame.
void gfxGpuTimerBeginFrame();
void gfxGpuTimerEndFrame();

// Named scope.  Nesting is fine; the report indents by depth.
void gfxGpuTimerPush(const char *name);
void gfxGpuTimerPop();

// Dump the accumulated table (also called once at shutdown).
void gfxGpuTimerReport(const char *why);

// Called once per resolved frame for each scope name, with that name's GPU ms
// summed over the frame.  pfProfiler installs one so GPU passes land on the
// EKG overlay and in -pflog next to the CPU timers; gfx cannot see profile.
typedef void (*gfxGpuTimerSink)(const char *name, float gpuMs);
void gfxGpuTimerSetSink(gfxGpuTimerSink sink);

class gfxGpuScope {
public:
    explicit gfxGpuScope(const char *name) { gfxGpuTimerPush(name); }
    ~gfxGpuScope() { gfxGpuTimerPop(); }
private:
    gfxGpuScope(const gfxGpuScope &);
    gfxGpuScope &operator=(const gfxGpuScope &);
};

// A scope that only times when the condition holds - one draw entry point can
// serve several kinds of model, and only some of them are worth a row.
class gfxGpuScopeIf {
public:
    gfxGpuScopeIf(bool on, const char *name) : mOn(on) { if (on) gfxGpuTimerPush(name); }
    ~gfxGpuScopeIf() { if (mOn) gfxGpuTimerPop(); }
private:
    bool mOn;
    gfxGpuScopeIf(const gfxGpuScopeIf &);
    gfxGpuScopeIf &operator=(const gfxGpuScopeIf &);
};

#define GPU_SCOPE_CAT2(a, b) a##b
#define GPU_SCOPE_CAT(a, b) GPU_SCOPE_CAT2(a, b)
#define GPU_SCOPE(name) gfxGpuScope GPU_SCOPE_CAT(gpuScope_, __LINE__)(name)
#define GPU_SCOPE_IF(cond, name) gfxGpuScopeIf GPU_SCOPE_CAT(gpuScope_, __LINE__)((cond), (name))

#else // !__WIN32PC

inline bool gfxGpuTimerEnabled() { return false; }
inline void gfxGpuTimerBeginFrame() {}
inline void gfxGpuTimerEndFrame() {}
inline void gfxGpuTimerPush(const char *) {}
inline void gfxGpuTimerPop() {}
inline void gfxGpuTimerReport(const char *) {}
typedef void (*gfxGpuTimerSink)(const char *name, float gpuMs);
inline void gfxGpuTimerSetSink(gfxGpuTimerSink) {}
#define GPU_SCOPE(name) ((void)0)
#define GPU_SCOPE_IF(cond, name) ((void)0)

#endif // __WIN32PC

#endif // GFX_GPUTIMER_H
