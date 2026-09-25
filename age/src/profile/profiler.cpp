////////////////////////////////////////
// profiler.cpp
//
// Performance Graph & EKG Instrumentation
////////////////////////////////////////

#include "profile/profiler.h"
#include "core/output.h"
#include "data/timemgr.h"
#include "data/args.h"
#include "profile/ekg.h"
#include "gfx/simple.h"
#include "gfx/font.h"
#include "gfx/gputimer.h"
#include "bank/bank.h"
#include "input/keyboard.h"
#include "input/keys.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>

static bool g_ShowProfilerOverlay = false;
static float g_GraphScaleMs = 33.3f;
static bool g_Initialized = false;

// Global registry of every pfTimer object so PF_START/PF_STOP/PF_SET can find
// metrics by name.  POD storage only: timers register during static init from
// arbitrary translation units, before any constructor in this file has run.
static const int MAX_PF_TIMERS = 512;
static pfTimer* g_AllTimers[MAX_PF_TIMERS];
static int g_AllTimerCount = 0;

u64 pfGetMicroseconds() {
    LARGE_INTEGER qpc, freq;
    QueryPerformanceCounter(&qpc);
    QueryPerformanceFrequency(&freq);
    if (freq.QuadPart == 0) return 0;
    return (u64)((qpc.QuadPart * 1000000ULL) / freq.QuadPart);
}

static inline u64 GetMicroseconds() { return pfGetMicroseconds(); }

////////////////////////////////////////
// pfGroup

pfGroup::pfGroup(const char* name) {
    // TimerCount/Timers are deliberately not reset here.  Groups are shared
    // across translation units (EXT_PF_GROUP), so a pfTimer in another TU may
    // have registered into this group before this constructor runs; static
    // zero-initialization has already cleared the fields at load time.
    strncpy(Name, name ? name : "", sizeof(Name) - 1);
    Name[sizeof(Name) - 1] = '\0';
}

void pfGroup::AddTimer(pfTimer* timer) {
    if (!timer || TimerCount >= MaxTimers) return;
    for (int i = 0; i < TimerCount; i++) {
        if (Timers[i] == timer) return;
    }
    Timers[TimerCount++] = timer;
}

////////////////////////////////////////
// pfPage

pfPage::pfPage(const char* name) {
    // GroupCount/Groups are not reset for the same cross-TU static-init
    // ordering reason as pfGroup.
    strncpy(Name, name ? name : "", sizeof(Name) - 1);
    Name[sizeof(Name) - 1] = '\0';
    pfEKGMgr::RegisterPage(this);
}

void pfPage::AddGroup(pfGroup* group) {
    if (!group || GroupCount >= MaxGroups) return;
    for (int i = 0; i < GroupCount; i++) {
        if (Groups[i] == group) return;
    }
    Groups[GroupCount++] = group;
}

#if __BANK
void pfPage::AddWidgets(bkBank &bank) {
    pfProfiler::AddWidgets(bank);
}
#endif // __BANK

////////////////////////////////////////
// pfTimer

pfTimer::pfTimer(const char* name, pfGroup* group, bool active)
    : Active(active) {
    strncpy(Name, name ? name : "", sizeof(Name) - 1);
    Name[sizeof(Name) - 1] = '\0';
    ResetSamples();
    StartTimeUs = 0;
    if (group) {
        group->AddTimer(this);
    }
    pfProfiler::RegisterTimerObject(this);
}

void pfTimer::ResetSamples() {
    memset(Samples, 0, sizeof(Samples));
    SampleHead = 0;
    SampleCount = 0;
    Cur = 0.0f;
    Avg = 0.0f;
    Max = 0.0f;
}

void pfTimer::Start() {
    StartTimeUs = GetMicroseconds();
}

void pfTimer::Stop() {
    if (StartTimeUs == 0) return;
    u64 endUs = GetMicroseconds();
    Record((float)(endUs - StartTimeUs) / 1000.0f);
    StartTimeUs = 0;
}

void pfTimer::Record(float value) {
    Cur = value;
    Samples[SampleHead] = value;
    SampleHead = (SampleHead + 1) % MaxSamples;
    if (SampleCount < MaxSamples) SampleCount++;

    float sum = 0.0f;
    float maxVal = 0.0f;
    for (int i = 0; i < SampleCount; i++) {
        sum += Samples[i];
        if (Samples[i] > maxVal) maxVal = Samples[i];
    }
    Avg = sum / (float)SampleCount;
    Max = maxVal;
}

////////////////////////////////////////
// pfEKGMgr - page registry + active page selection

static pfPage* s_EKGPages[32];
static int s_EKGPageCount = 0;
static int s_SelectedEKGPageIndex = 0;

void pfEKGMgr::RegisterPage(pfPage* page) {
    if (!page) return;
    // Dedupe by pointer only: scroni constructs several pages with empty
    // names and fills them in afterwards, so names are not unique here.
    for (int i = 0; i < s_EKGPageCount; i++) {
        if (s_EKGPages[i] == page) return;
    }
    if (s_EKGPageCount < 32) {
        s_EKGPages[s_EKGPageCount++] = page;
    }
}

pfPage* pfEKGMgr::GetPage() {
    if (s_EKGPageCount > 0 && s_SelectedEKGPageIndex >= 0 && s_SelectedEKGPageIndex < s_EKGPageCount) {
        return s_EKGPages[s_SelectedEKGPageIndex];
    }
    return nullptr;
}

void pfEKGMgr::CyclePage() {
    if (s_EKGPageCount > 0) {
        s_SelectedEKGPageIndex = (s_SelectedEKGPageIndex + 1) % s_EKGPageCount;
    }
}

#if __BANK
void pfEKGMgr::AddWidgets(bkBank &bank) {
    if (s_EKGPageCount > 0) {
        static const char* pageNames[32];
        for (int i = 0; i < s_EKGPageCount; i++) {
            pageNames[i] = s_EKGPages[i]->GetName();
        }
        bank.AddCombo("Active EKG Page", &s_SelectedEKGPageIndex, s_EKGPageCount, pageNames, 0, NullCB, "Select active EKG profiler page");
    }
}
#endif // __BANK

////////////////////////////////////////
// Built-in "Core" page + fallback timer pool
//
// Core loop timers (FrameTime etc.) and any PF_SET/PF_START name that no
// PF_TIMER/PF_VALUE object claims are backed by this pool, shown on their
// own EKG page.

static pfPage s_CorePage("Core");
static pfGroup s_CoreGroup("Core");
static const pfLinkAutoReg s_CoreLink(s_CorePage, s_CoreGroup);

static const int MAX_POOL_TIMERS = 32;
static pfTimer s_PoolTimers[MAX_POOL_TIMERS];

static pfTimer* ClaimPoolTimer(const char* name) {
    if (!name || !*name) return nullptr;
    for (int i = 0; i < MAX_POOL_TIMERS; i++) {
        if (s_PoolTimers[i].Name[0] == '\0') {
            strncpy(s_PoolTimers[i].Name, name, pfTimer::NameMaxLen - 1);
            s_PoolTimers[i].Name[pfTimer::NameMaxLen - 1] = '\0';
            s_PoolTimers[i].Active = true;
            s_CoreGroup.AddTimer(&s_PoolTimers[i]);
            return &s_PoolTimers[i];
        }
    }
    return nullptr;
}

// -gputime passes (gfx/gputimer), fed from D3D timestamp queries a few frames
// after the fact.  A pool of its own so it cannot starve Core; the scope names
// ("fx:Glow.Draw", "draw:CarModel") are chosen not to collide with the CPU
// timers, since FindTimer searches this pool too.
// A pass that did not draw in a frame records no sample, so Avg is per frame
// drawn, same as the -gputime table.

static pfPage s_GpuPage("GPU");
static pfGroup s_GpuGroup("GPU");
static const pfLinkAutoReg s_GpuLink(s_GpuPage, s_GpuGroup);

static pfTimer s_GpuTimers[pfGroup::MaxTimers];
static int s_GpuTimerCount;

static void RecordGpuTimer(const char* name, float gpuMs) {
    for (int i = 0; i < s_GpuTimerCount; i++) {
        if (strcmp(s_GpuTimers[i].Name, name) == 0) {
            s_GpuTimers[i].Record(gpuMs);
            return;
        }
    }
    if (s_GpuTimerCount >= pfGroup::MaxTimers) return;
    pfTimer& t = s_GpuTimers[s_GpuTimerCount++];
    strncpy(t.Name, name, pfTimer::NameMaxLen - 1);
    t.Name[pfTimer::NameMaxLen - 1] = '\0';
    t.Active = true;
    s_GpuGroup.AddTimer(&t);
    t.Record(gpuMs);
}

////////////////////////////////////////
// pfProfiler

void pfProfiler::RegisterTimerObject(pfTimer* timer) {
    if (!timer || g_AllTimerCount >= MAX_PF_TIMERS) return;
    g_AllTimers[g_AllTimerCount++] = timer;
}

pfTimer* pfProfiler::FindTimer(const char* name) {
    if (!name || !*name) return nullptr;
    for (int i = 0; i < g_AllTimerCount; i++) {
        if (g_AllTimers[i]->Name[0] && _stricmp(g_AllTimers[i]->Name, name) == 0) {
            return g_AllTimers[i];
        }
    }
    return nullptr;
}

void pfProfiler::Init() {
    if (g_Initialized) return;
    g_Initialized = true;

    RegisterTimer("FrameTime");
    RegisterTimer("UpdateLoop");
    RegisterTimer("DrawLoop");
    RegisterTimer("Physics");
    RegisterTimer("AI");
    RegisterTimer("Animation");
    RegisterTimer("Render");

    gfxGpuTimerSetSink(RecordGpuTimer);
}

void pfProfiler::Reset() {
    for (int i = 0; i < g_AllTimerCount; i++) {
        g_AllTimers[i]->ResetSamples();
        g_AllTimers[i]->StartTimeUs = 0;
    }
}

void pfProfiler::BeginFrame() {
    Init();
    StartTimer("FrameTime");
}

void pfProfiler::EndFrame() {
    StopTimer("FrameTime");

    // -pflog [seconds]: periodic text dump.  Default every 5 s.
    static int sEnabled = -1;
    static float sPeriod = 5.0f;
    if (sEnabled < 0) {
        const char *v = 0;
        sEnabled = (args::sm_Instance && ARGS.Get("pflog", 0, &v)) ? 1 : 0;
        if (sEnabled && v && v[0] && v[0] != '-') {
            const float p = (float)atof(v);
            if (p > 0.0f) sPeriod = p;
        }
    }
    if (sEnabled) {
        static float sNext = 0.0f;
        const float now = TIME.GetElapsedTime();
        if (now >= sNext) {
            sNext = now + sPeriod;
            LogTimers(0);
        }
    }
}


void pfProfiler::UpdateInput() {
    if (ioKeyboard::KeyPressed(KEY_F3)) {
        g_ShowProfilerOverlay = !g_ShowProfilerOverlay;
    }
    if (ioKeyboard::KeyPressed(KEY_F4)) {
        pfEKGMgr::CyclePage();
    }
}

void pfProfiler::Update() {}

void pfProfiler::Shutdown() {
    Reset();
    g_Initialized = false;
}

void pfProfiler::RegisterTimer(const char* name) {
    if (!FindTimer(name)) {
        ClaimPoolTimer(name);
    }
}

void pfProfiler::StartTimer(const char* name) {
    Init();
    pfTimer* t = FindTimer(name);
    if (!t) t = ClaimPoolTimer(name);
    if (t) t->Start();
}

void pfProfiler::StopTimer(const char* name) {
    pfTimer* t = FindTimer(name);
    if (t) t->Stop();
}

void pfProfiler::RecordValue(const char* name, float val) {
    Init();
    pfTimer* t = FindTimer(name);
    if (!t) t = ClaimPoolTimer(name);
    if (t) t->Record(val);
}

#if __BANK
void pfProfiler::AddWidgets(bkBank &bank) {
    Init();
    bank.PushGroup("Performance Overlay", false);
    bank.AddToggle("Show EKG Overlay", &g_ShowProfilerOverlay, NullCB, "Toggle onscreen EKG performance graph overlay");
    pfEKGMgr::AddWidgets(bank);
    bank.AddSlider("Min Scale (ms)", &g_GraphScaleMs, 1.0f, 100.0f, 1.0f, NullCB, "Minimum full-height scale; grows automatically to fit the data");
    bank.PopGroup();
}
#endif // __BANK

////////////////////////////////////////
// Draw

static const u32 EGA_PALETTE[16] = {
    0xFF00FFFF, // 0: Cyan
    0xFFFF00FF, // 1: Magenta
    0xFFFFFF00, // 2: Yellow
    0xFF00FF00, // 3: Green
    0xFFFF5555, // 4: Light Red
    0xFF5555FF, // 5: Light Blue
    0xFFFFAA00, // 6: Orange
    0xFFAA55FF, // 7: Purple
    0xFF00FFAA, // 8: Teal
    0xFFFF88AA, // 9: Pink
    0xFFAAFF00, // 10: Lime
    0xFFFFFFFF, // 11: White
    0xFF55FFFF, // 12: Bright Cyan
    0xFFFF55FF, // 13: Bright Magenta
    0xFFFFFF55, // 14: Bright Yellow
    0xFF55FF55  // 15: Bright Green
};

void pfProfiler::Draw() {
    if (!g_ShowProfilerOverlay) return;
    Init();

    pfPage* activePage = pfEKGMgr::GetPage();

    // Gather the metrics to plot: every named, active timer in every group
    // linked to the active page.  The palette bounds how many tracks stay
    // distinguishable.
    static const int MAX_TRACKS = 16;
    pfTimer* tracks[MAX_TRACKS];
    int numTracks = 0;
    if (activePage) {
        for (int gi = 0; gi < activePage->GroupCount && numTracks < MAX_TRACKS; gi++) {
            pfGroup* group = activePage->Groups[gi];
            for (int ti = 0; ti < group->TimerCount && numTracks < MAX_TRACKS; ti++) {
                pfTimer* t = group->Timers[ti];
                if (t && t->Active && t->Name[0]) {
                    tracks[numTracks++] = t;
                }
            }
        }
    }

    int gx = 20;
    int gy = 240;
    int gw = 320;
    int gh = 140;

    PIPE.ClearRect(gx, gy, gw, gh, (u32)0xDD10141C);

    // The slider sets the minimum full-height scale; grow it to fit the data
    // so value pages (memory KB etc.) stay on the graph.
    float scale = (g_GraphScaleMs > 0.001f) ? g_GraphScaleMs : 33.3f;
    for (int i = 0; i < numTracks; i++) {
        if (tracks[i]->Max * 1.05f > scale) scale = tracks[i]->Max * 1.05f;
    }

    char header[128];
    sprintf(header, "EKG: [%s] Scale: %.1f", activePage ? activePage->GetName() : "(no page)", scale);
    gfxDrawFont(gx + 8, gy + 6, header, 0xFF00FFFF);

    int startX = gx + 8;
    int plotW = gw - 16;
    int plotH = gh - 26;
    int plotBottom = gy + gh - 8;

    // Frame-budget guide lines.
    if (16.6f <= scale) {
        int y16 = plotBottom - (int)((16.6f / scale) * plotH);
        if (y16 >= gy + 20 && y16 <= gy + gh - 8) {
            PIPE.ClearRect(gx + 8, y16, gw - 16, 1, (u32)0x6000FF00);
            gfxDrawFont(gx + gw - 60, y16 - 6, "16.6ms", 0x8000FF00);
        }
    }
    if (33.3f <= scale) {
        int y33 = plotBottom - (int)((33.3f / scale) * plotH);
        if (y33 >= gy + 20 && y33 <= gy + gh - 8) {
            PIPE.ClearRect(gx + 8, y33, gw - 16, 1, (u32)0x60FFFF00);
            gfxDrawFont(gx + gw - 60, y33 - 6, "33.3ms", 0x80FFFF00);
        }
    }

    // One EKG trace per metric, batched as a single line strip each.
    static const int PLOT_POINTS = 150;
    float pts[PLOT_POINTS * 2];
    for (int i = 0; i < numTracks; i++) {
        pfTimer* t = tracks[i];
        u32 lineColor = EGA_PALETTE[i % 16];

        int numPoints = t->SampleCount < PLOT_POINTS ? t->SampleCount : PLOT_POINTS;
        if (numPoints < 2) continue;
        for (int k = 0; k < numPoints; k++) {
            int idx = (t->SampleHead - numPoints + k + pfTimer::MaxSamples) % pfTimer::MaxSamples;
            float norm = t->Samples[idx] / scale;
            if (norm > 1.0f) norm = 1.0f;
            if (norm < 0.0f) norm = 0.0f;
            pts[k * 2 + 0] = (float)(startX + (k * plotW) / PLOT_POINTS);
            pts[k * 2 + 1] = (float)(plotBottom - (int)(norm * plotH));
        }
        gfxDrawPolyline2D(pts, numPoints, lineColor);
    }

    // Legend panel: color key + current value for every plotted metric.
    int lx = gx + gw + 10;
    int ly = gy;
    int lw = 200;
    int lh = 20 + numTracks * 13 + 8;
    if (lh < gh) lh = gh;

    PIPE.ClearRect(lx, ly, lw, lh, (u32)0xDD10141C);
    gfxDrawFont(lx + 8, ly + 6, "Legend (Key)", 0xFFFFFFFF);
    for (int i = 0; i < numTracks; i++) {
        pfTimer* t = tracks[i];
        u32 lineColor = EGA_PALETTE[i % 16];
        int itemY = ly + 20 + i * 13;
        PIPE.ClearRect(lx + 8, itemY + 2, 8, 8, lineColor);
        char legendEntry[64];
        sprintf(legendEntry, "%.14s: %.1f", t->Name, t->Cur);
        gfxDrawFont(lx + 20, itemY, legendEntry, lineColor);
    }
    if (numTracks == 0) {
        gfxDrawFont(lx + 8, ly + 20, "(no metrics on page)", 0xFFAAAAAA);
    }
}

// Worst-first so the line that matters is the first one read.
void pfProfiler::LogTimers(const char *why) {
    Displayf("[pflog] ---- %s%s----", why ? why : "", why ? " " : "");
    for (int p = 0; p < s_EKGPageCount; p++) {
        pfPage *page = s_EKGPages[p];
        if (!page) continue;
        for (int g = 0; g < page->GroupCount; g++) {
            pfGroup *grp = page->Groups[g];
            if (!grp || grp->TimerCount == 0) continue;

            // index sort, so the timers themselves are not disturbed
            int order[pfGroup::MaxTimers];
            int n = 0;
            for (int t = 0; t < grp->TimerCount && n < pfGroup::MaxTimers; t++)
                if (grp->Timers[t]) order[n++] = t;
            for (int a = 1; a < n; a++) {
                const int key = order[a];
                int b = a - 1;
                while (b >= 0 && grp->Timers[order[b]]->Avg < grp->Timers[key]->Avg) {
                    order[b + 1] = order[b];
                    b--;
                }
                order[b + 1] = key;
            }

            bool printedHeader = false;
            for (int i = 0; i < n; i++) {
                const pfTimer *tm = grp->Timers[order[i]];
                if (tm->Avg < 0.01f && tm->Max < 0.01f) continue;   // idle
                if (!printedHeader) {
                    Displayf("[pflog] %s / %s", page->GetName(), grp->GetName());
                    printedHeader = true;
                }
                Displayf("[pflog]     %-24s avg %7.3f ms   max %7.3f ms   cur %7.3f ms",
                         tm->GetName(), tm->Avg, tm->Max, tm->Cur);
            }
        }
    }
}
