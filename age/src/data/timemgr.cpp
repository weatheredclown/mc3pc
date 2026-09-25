////////////////////////////////////////
// timemgr.cpp
////////////////////////////////////////

#include "data/timemgr.h"
#include "data/args.h"
#include <windows.h>

timeManager *timeManager::sm_Instance = NULL;
static timeManager s_TimeManager;

timeManager::timeManager() {
    sm_Instance = this;
    m_RealTime = true;
    m_FixedFps = 60;
    m_ElapsedTime = 0.0f;
    m_DeltaSeconds = 0.016f;
    m_UnwarpedSeconds = 0.016f;
    m_ActualSeconds = 0.016f;
    m_TimeWarp = 1.0f;
    m_MaxDelta = 0.1f;   // 100 ms (== 10 fps floor)
    m_FrameCount = 0;

    m_SmoothNext = 0;
    m_SmoothWindow = 8;          // see SmoothDelta(); -nodtsmooth turns it off
    m_SmoothChecked = false;
    for (int i = 0; i < MaxSmoothWindow; i++)
        m_SmoothRing[i] = 0.0f;

    LARGE_INTEGER freq, count;
    QueryPerformanceFrequency(&freq);
    m_Frequency = (double)freq.QuadPart;

    QueryPerformanceCounter(&count);
    m_StartCounter = count.QuadPart;
    m_LastCounter = count.QuadPart;
}

void timeManager::RealTime() {
    m_RealTime = true;
}

void timeManager::FixedFrame(int fps) {
    m_RealTime = false;
    m_FixedFps = fps;
}

//---------------------------------------------------------------------------
// -dtsmooth [n]: average the frame delta over the last n frames (default 8).
//
// The simulation integrates whatever delta it is handed, so it places objects
// correctly for the time a frame took to *produce* - but the frame is then
// displayed for however long the next one takes to arrive, and on this port
// those two differ by more than 4 ms on about one frame in fifteen.  Averaging
// hands the sim a delta close to the cadence actually being displayed.
//
// The mean, not the median: a mean preserves total elapsed time, so game time
// still tracks the wall clock, and a one-off hitch is spread thinly across the
// window instead of arriving as one visible jump.
//---------------------------------------------------------------------------
float timeManager::SmoothDelta(float delta) {
    if (!m_SmoothChecked) {
        m_SmoothChecked = true;
        // On by default: measured over three runs each, smoothing plus -fpscap
        // took the frame-to-frame variation in the car's position relative to
        // the camera from 134% down to 43%, and nothing was found that it costs.
        // -dtsmooth <n> picks another window; -nodtsmooth restores raw deltas.
        int window = 0;
        if (ARGS.Get("dtsmooth", 0, window) && window > 0)
            m_SmoothWindow = window;
        if (ARGS.Get("nodtsmooth"))
            m_SmoothWindow = 0;
        if (m_SmoothWindow > MaxSmoothWindow)
            m_SmoothWindow = MaxSmoothWindow;
    }

    if (m_SmoothWindow <= 1)
        return delta;

    m_SmoothRing[m_SmoothNext % MaxSmoothWindow] = delta;
    m_SmoothNext++;

    const int have = (m_SmoothNext < m_SmoothWindow) ? m_SmoothNext : m_SmoothWindow;
    double sum = 0.0;
    for (int i = 0; i < have; i++)
        sum += m_SmoothRing[(m_SmoothNext - 1 - i + MaxSmoothWindow * 2) % MaxSmoothWindow];

    return (float)(sum / (double)have);
}

void timeManager::Update() {
    // -fixedfps <n>: run every frame as if it took exactly 1/n seconds, so a
    // frame-rate dependent bug can be reproduced at a chosen rate instead of
    // whatever the machine happens to render.  Checked on the first tick, which
    // is after the command line has been parsed.
    static int s_fixedChecked = 0;
    if (!s_fixedChecked) {
        s_fixedChecked = 1;
        int fps = 0;
        if (ARGS.Get("fixedfps", 0, fps) && fps > 0)
            FixedFrame(fps);
    }

    m_FrameCount++;
    if (m_RealTime) {
        LARGE_INTEGER count;
        QueryPerformanceCounter(&count);
        double delta = (double)(count.QuadPart - m_LastCounter) / m_Frequency;

        // Clamp the frame delta. After a stall (level load, a debugger break,
        // alt-tab, a GC hitch) the raw wall-clock delta can be seconds long.
        // Feeding that straight into fixed-per-frame integrators (gravity in
        // the mover, etc.) makes a single step move the actor tens of metres --
        // e.g. the player teleports through the level instead of falling. When
        // we can't keep up with realtime we'd rather run the game slower for a
        // frame than take one enormous step, so game time (m_ElapsedTime) also
        // advances by the clamped amount rather than raw wall-clock.
        if (delta < 0.0) delta = 0.0;  // guard against counter going backwards
        m_ActualSeconds = (float)delta;
        if (delta > (double)m_MaxDelta) delta = (double)m_MaxDelta;

        m_UnwarpedSeconds = SmoothDelta((float)delta);
        m_LastCounter = count.QuadPart;
    } else {
        m_UnwarpedSeconds = 1.0f / (float)m_FixedFps;
        m_ActualSeconds = m_UnwarpedSeconds;
    }
    m_DeltaSeconds = m_UnwarpedSeconds * m_TimeWarp;
    m_ElapsedTime += m_DeltaSeconds;
}

void timeManager::Reset() {
    LARGE_INTEGER count;
    QueryPerformanceCounter(&count);
    m_LastCounter = count.QuadPart;
    m_DeltaSeconds = m_UnwarpedSeconds = m_ActualSeconds = 0.0f;
}

float timeManager::GetElapsedTime() const {
    return m_ElapsedTime;
}

float timeManager::GetSeconds() const {
    return m_DeltaSeconds;
}

float timeManager::GetInvSeconds() const {
    return m_DeltaSeconds > 0.0f ? 1.0f / m_DeltaSeconds : 0.0f;
}

float timeManager::GetUnwarpedSeconds() const {
    return m_UnwarpedSeconds;
}

unsigned timeManager::GetFrameCount() const {
    return m_FrameCount;
}

// Timer class implementation
// Timer class implementation
Timer::Timer() {
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    m_Frequency = (double)freq.QuadPart;
    Reset();
}

void Timer::Reset() {
    LARGE_INTEGER count;
    QueryPerformanceCounter(&count);
    m_StartCounter = count.QuadPart;
}

float Timer::Time() const {
    LARGE_INTEGER count;
    QueryPerformanceCounter(&count);
    return (float)((double)(count.QuadPart - m_StartCounter) / m_Frequency);
}

#include <chrono>
#include <thread>

void Timer::Sleep(int ms)
{
    if (ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}
