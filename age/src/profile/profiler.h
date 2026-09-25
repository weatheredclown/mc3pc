////////////////////////////////////////
// profiler.h
////////////////////////////////////////

#ifndef PROFILE_PROFILER_H
#define PROFILE_PROFILER_H

#include "core/types.h"
#include "gfx/model.h"
#include "bank/bank.h"

class pfTimer;

// A named collection of timers/values.  Group objects may be shared across
// translation units via EXT_PF_GROUP, so timers can register into a group
// whose constructor has not run yet (see the ctor for the ordering rules).
class pfGroup {
public:
    char Name[64];
    static const int NameMaxLen = 64;
    enum { MaxTimers = 48 };
    pfTimer* Timers[MaxTimers];
    int TimerCount;

    pfGroup(const char* name = "");
    const char* GetName() const { return Name; }
    void AddTimer(pfTimer* timer);
};

// A named EKG page.  The overlay draws every active timer in every group
// linked to the selected page.  Pages register themselves with pfEKGMgr on
// construction.
class pfPage {
public:
    char Name[64];
    static const int NameMaxLen = 64;
    enum { MaxGroups = 8 };
    pfGroup* Groups[MaxGroups];
    int GroupCount;

    pfPage(const char* name = "");
    const char* GetName() const { return Name; }
    void AddGroup(pfGroup* group);
    void AddWidgets(class bkBank &bank);
};

// A single metric: either a timed section (Start/Stop, milliseconds) or a
// recorded value (Record / PF_SET).  Keeps a rolling sample history for the
// EKG waveform.
class pfTimer {
public:
    char Name[64];
    static const int NameMaxLen = 64;
    bool Active;

    enum { MaxSamples = 160 };
    float Samples[MaxSamples];
    int SampleHead;
    int SampleCount;
    float Cur;
    float Avg;
    float Max;
    u64 StartTimeUs;

    pfTimer(const char* name = "", pfGroup* group = nullptr, bool active = true);
    const char* GetName() const { return Name; }
    void Start();
    void Stop();
    void Record(float value);
    void ResetSamples();
};

class pfProfiler {
public:
    static void Init();
    static void Reset();
    static void BeginFrame();
    static void EndFrame();
    static void UpdateInput();
    static void Update();
    static void Update(bool paused) { (void)paused; Update(); }
    static void Draw();

    static void RegisterTimer(const char* name);
    static void StartTimer(const char* name);
    static void StopTimer(const char* name);
    static void RecordValue(const char* name, float val);

    static pfTimer* FindTimer(const char* name);
    static void RegisterTimerObject(pfTimer* timer);

    static void AddWidgets(class bkBank &bank);
    static void Shutdown();

    // -pflog [seconds]: write every page's timers to the log on a timer, worst
    // first.  The EKG overlay is the nicer read, but a profile you can diff,
    // grep and paste into a bug beats one you have to squint at - and a
    // headless run (-shotframes/-quitafter) has no overlay at all.
    static void LogTimers(const char *why = 0);
};

class pfPageLink {
public:
    pfPageLink(pfPage *page, pfGroup *group) { page->AddGroup(group); }
};

struct pfLinkAutoReg {
    pfLinkAutoReg(pfPage& page, pfGroup& group) {
        page.AddGroup(&group);
    }
};

#define PF_START(x) pfProfiler::StartTimer(#x)
#define PF_STOP(x)  pfProfiler::StopTimer(#x)
// Pages and groups may share a name (PF_PAGE(MCRender) + PF_GROUP(MCRender)),
// so their symbols are prefixed.
#define PF_PAGE(x, y)      pfPage PFPAGE_##x(y)
#define PF_GROUP(x)        pfGroup PFGROUP_##x(#x)
#define PF_LINK(x, y)      pfPageLink PFPAGELINK_##x##_##y(&PFPAGE_##x, &PFGROUP_##y)
#define PF_TIMER(x, y)     static pfTimer x(#x, &PFGROUP_##y)
#define PF_VALUE_INT(x, y) static pfTimer x(#x, &PFGROUP_##y)
#define PF_VALUE_FLOAT(x, y) static pfTimer x(#x, &PFGROUP_##y)

#define EXT_PF_PAGE(x)   extern pfPage PFPAGE_##x
#define EXT_PF_GROUP(x)  extern pfGroup PFGROUP_##x
#define EXT_PF_TIMER(x)  extern pfTimer x
#define EXT_PF_LINK(x, y) extern pfPageLink PFPAGELINK_##x##_##y

// Free-running wall clock, in microseconds.  Cheap enough to call inside a
// frame without changing what it measures.
u64 pfGetMicroseconds();

// The engine profiler instance (all-static class; the object only gives it a name).
inline pfProfiler &GetAGEProfiler() { static pfProfiler s_Profiler; return s_Profiler; }

#endif // PROFILE_PROFILER_H
