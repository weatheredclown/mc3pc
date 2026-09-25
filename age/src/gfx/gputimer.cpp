////////////////////////////////////////
// gputimer.cpp
//
// D3D11 timestamp-query profiler.  See gputimer.h for why the fx passes need
// GPU numbers rather than CPU ones.
////////////////////////////////////////

#include "gfx/gputimer.h"

#if __WIN32PC

#include "data/args.h"
#include <d3d11.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" ID3D11Device *gfxGetDevice();
extern "C" ID3D11DeviceContext *gfxGetContext();

namespace {

enum {
    // Query results lag the submission that made them by a few frames, so the
    // slots have to outnumber the frames the driver may buffer.
    NumSlots = 5,
    // One entry per Push per frame; the whole fx breakdown fits well inside.
    MaxScopesPerFrame = 256,
    MaxNames = 128,
};

struct Scope {
    const char *Name;
    int Depth;
    ID3D11Query *Begin;
    ID3D11Query *End;
    double CpuMs;
};

struct Slot {
    ID3D11Query *Disjoint;
    Scope Scopes[MaxScopesPerFrame];
    int ScopeCount;
    bool Pending;
};

// One row of the report: a scope name summed over every frame that resolved.
struct NameStat {
    const char *Name;
    int Depth;
    double GpuMs;
    double CpuMs;
    double GpuMaxMs;
    int Calls;
    int Frames;
};

bool sInited;
bool sEnabled;
double sReportInterval = 5.0;
Slot sSlots[NumSlots];
int sSlot;
int sStack[16];
int sStackDepth;
NameStat sStats[MaxNames];
int sStatCount;
int sFramesResolved;
LARGE_INTEGER sQpcFreq;
double sLastReport;
double sFrameStart;
double sCpuFrameMs;
int sCpuFrames;
double sWallStart;
double sWallFrames;
gfxGpuTimerSink sSink;

double Now() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart / (double)sQpcFreq.QuadPart;
}

NameStat *FindStat(const char *name, int depth) {
    for (int i = 0; i < sStatCount; i++) {
        if (!strcmp(sStats[i].Name, name))
            return &sStats[i];
    }
    if (sStatCount >= MaxNames)
        return NULL;
    NameStat *s = &sStats[sStatCount++];
    s->Name = name;
    s->Depth = depth;
    s->GpuMs = s->CpuMs = s->GpuMaxMs = 0.0;
    s->Calls = s->Frames = 0;
    return s;
}

void Init() {
    sInited = true;
    QueryPerformanceFrequency(&sQpcFreq);
    const char *v = NULL;
    if (!ARGS.Get("gputime", 0, &v))
        return;
    if (v && *v) {
        double iv = atof(v);
        if (iv > 0.0)
            sReportInterval = iv;
    }
    ID3D11Device *dev = gfxGetDevice();
    if (!dev)
        return;
    for (int i = 0; i < NumSlots; i++) {
        D3D11_QUERY_DESC qd;
        qd.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
        qd.MiscFlags = 0;
        if (FAILED(dev->CreateQuery(&qd, &sSlots[i].Disjoint)))
            return;
        qd.Query = D3D11_QUERY_TIMESTAMP;
        for (int s = 0; s < MaxScopesPerFrame; s++) {
            if (FAILED(dev->CreateQuery(&qd, &sSlots[i].Scopes[s].Begin)) ||
                FAILED(dev->CreateQuery(&qd, &sSlots[i].Scopes[s].End)))
                return;
        }
    }
    sEnabled = true;
    sLastReport = Now();
    sWallStart = sLastReport;
    printf("[gputime] per-pass GPU timing on, reporting every %.1fs\n", sReportInterval);
    fflush(stdout);
}

// Pull one finished slot's timestamps into the running table.  DONOTFLUSH: a
// profiler that flushes the context changes the thing it is measuring.
void Resolve(Slot &slot) {
    if (!slot.Pending)
        return;
    ID3D11DeviceContext *ctx = gfxGetContext();
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj;
    if (ctx->GetData(slot.Disjoint, &dj, sizeof(dj), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
        return;
    slot.Pending = false;
    if (dj.Disjoint || !dj.Frequency)
        return;
    double scopeMs[MaxScopesPerFrame];
    for (int i = 0; i < slot.ScopeCount; i++) {
        Scope &sc = slot.Scopes[i];
        scopeMs[i] = -1.0;
        UINT64 b = 0, e = 0;
        if (ctx->GetData(sc.Begin, &b, sizeof(b), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK ||
            ctx->GetData(sc.End, &e, sizeof(e), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
            continue;
        if (e < b)
            continue;
        double ms = (double)(e - b) * 1000.0 / (double)dj.Frequency;
        scopeMs[i] = ms;
        NameStat *st = FindStat(sc.Name, sc.Depth);
        if (!st)
            continue;
        st->GpuMs += ms;
        st->CpuMs += sc.CpuMs;
        if (ms > st->GpuMaxMs)
            st->GpuMaxMs = ms;
        st->Calls++;
    }
    // Frames are counted per name, not per scope, so a pass drawn twice in one
    // frame still reports a per-frame cost rather than a per-call one.
    for (int i = 0; i < sStatCount; i++) {
        bool seen = false;
        for (int j = 0; j < slot.ScopeCount && !seen; j++)
            seen = !strcmp(slot.Scopes[j].Name, sStats[i].Name);
        if (seen)
            sStats[i].Frames++;
    }
    // One sample per name per frame, same per-frame rule as above.
    if (sSink) {
        for (int i = 0; i < slot.ScopeCount; i++) {
            bool first = true;
            for (int j = 0; j < i && first; j++)
                first = strcmp(slot.Scopes[j].Name, slot.Scopes[i].Name) != 0;
            if (!first)
                continue;
            double sum = 0.0;
            bool any = false;
            for (int j = i; j < slot.ScopeCount; j++) {
                if (scopeMs[j] >= 0.0 && !strcmp(slot.Scopes[j].Name, slot.Scopes[i].Name)) {
                    sum += scopeMs[j];
                    any = true;
                }
            }
            if (any)
                sSink(slot.Scopes[i].Name, (float)sum);
        }
    }
    sFramesResolved++;
}

} // namespace

bool gfxGpuTimerEnabled() { return sEnabled; }

void gfxGpuTimerSetSink(gfxGpuTimerSink sink) { sSink = sink; }

void gfxGpuTimerBeginFrame() {
    if (!sInited)
        Init();
    if (!sEnabled)
        return;
    sFrameStart = Now();
    sSlot = (sSlot + 1) % NumSlots;
    Slot &slot = sSlots[sSlot];
    Resolve(slot);            // the slot about to be reused is the oldest one
    slot.ScopeCount = 0;
    slot.Pending = false;
    sStackDepth = 0;
    gfxGetContext()->Begin(slot.Disjoint);
}

void gfxGpuTimerPush(const char *name) {
    if (!sEnabled)
        return;
    Slot &slot = sSlots[sSlot];
    if (slot.ScopeCount >= MaxScopesPerFrame || sStackDepth >= 16)
        return;
    int idx = slot.ScopeCount++;
    Scope &sc = slot.Scopes[idx];
    sc.Name = name;
    sc.Depth = sStackDepth;
    sc.CpuMs = -Now();        // completed on Pop
    sStack[sStackDepth++] = idx;
    gfxGetContext()->End(sc.Begin);
}

void gfxGpuTimerPop() {
    if (!sEnabled || sStackDepth <= 0)
        return;
    Scope &sc = sSlots[sSlot].Scopes[sStack[--sStackDepth]];
    sc.CpuMs = (sc.CpuMs + Now()) * 1000.0;
    gfxGetContext()->End(sc.End);
}

void gfxGpuTimerEndFrame() {
    if (!sEnabled)
        return;
    while (sStackDepth > 0)
        gfxGpuTimerPop();
    Slot &slot = sSlots[sSlot];
    gfxGetContext()->End(slot.Disjoint);
    slot.Pending = true;

    double now = Now();
    sCpuFrameMs += (now - sFrameStart) * 1000.0;
    sCpuFrames++;
    sWallFrames++;

    if (now - sLastReport >= sReportInterval) {
        gfxGpuTimerReport(NULL);
        sLastReport = now;
    }
}

void gfxGpuTimerReport(const char *why) {
    if (!sEnabled || !sFramesResolved)
        return;
    double now = Now();
    double wall = now - sWallStart;
    printf("[gputime]%s%s  %.0f frames, %.1f fps, cpu-frame %.2f ms\n",
           why ? " " : "", why ? why : "",
           sWallFrames, wall > 0.0 ? sWallFrames / wall : 0.0,
           sCpuFrames ? sCpuFrameMs / sCpuFrames : 0.0);
    printf("[gputime]   %-34s %9s %9s %9s %7s\n", "pass", "gpu ms/f", "gpu max", "cpu ms/f", "calls/f");
    // Worst first: a profile you can sort by cost is the one you can act on.
    for (int i = 0; i < sStatCount; i++) {
        for (int j = i + 1; j < sStatCount; j++) {
            double a = sStats[i].Frames ? sStats[i].GpuMs / sStats[i].Frames : 0.0;
            double b = sStats[j].Frames ? sStats[j].GpuMs / sStats[j].Frames : 0.0;
            if (b > a) {
                NameStat t = sStats[i];
                sStats[i] = sStats[j];
                sStats[j] = t;
            }
        }
    }
    for (int i = 0; i < sStatCount; i++) {
        NameStat &s = sStats[i];
        if (!s.Frames)
            continue;
        printf("[gputime]   %-34s %9.3f %9.3f %9.3f %7.2f\n", s.Name,
               s.GpuMs / s.Frames, s.GpuMaxMs, s.CpuMs / s.Frames,
               (double)s.Calls / (double)s.Frames);
    }
    fflush(stdout);
}

#endif // __WIN32PC
