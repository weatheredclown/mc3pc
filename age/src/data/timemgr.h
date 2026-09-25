////////////////////////////////////////
// timemgr.h
////////////////////////////////////////

#ifndef DATA_TIMEMGR_H
#define DATA_TIMEMGR_H

#include "core/types.h"

class timeManager {
public:
    static timeManager *sm_Instance;

    timeManager();
    void RealTime();
    void FixedFrame(int fps);
    void FixedFrame(float fps, bool allowOverrun) { (void)allowOverrun; FixedFrame((int)(fps + 0.5f)); }
    void Update();
    void Reset();

    // Game time this frame (after warp), its reciprocal, and the wall-clock
    // delta before warp and before the stall clamp.
    float GetElapsedTime() const;
    float GetSeconds() const;
    float GetInvSeconds() const;
    float GetUnwarpedSeconds() const;
    float GetActualSeconds() const { return m_ActualSeconds; }
    float GetUnwarpedRealtimeSeconds() const { return m_ActualSeconds; }
    void SetSeconds(float s) { m_DeltaSeconds = s; m_UnwarpedSeconds = s; }

    // Time warp (slow motion / fast forward): scales the per-frame delta.
    float GetTimeWarp() const { return m_TimeWarp; }
    void SetTimeWarp(float warp) { m_TimeWarp = warp > 0.0f ? warp : 0.0f; }

    // Largest frame delta accepted after a stall, in seconds.
    void SetClamp(float maxDelta) { m_MaxDelta = maxDelta; }
    void SetClamp(float minDelta, float maxDelta) { (void)minDelta; m_MaxDelta = maxDelta; }   // lower bound: frames never run faster than real time here
    float GetClamp() const { return m_MaxDelta; }

    unsigned GetFrameCount() const;
    void SetElapsedTime(float t) { m_ElapsedTime = t; }
    void SetFrameCount(unsigned c) { m_FrameCount = c; }

private:
    bool m_RealTime;
    int m_FixedFps;
    float m_ElapsedTime;
    float m_DeltaSeconds;
    float m_UnwarpedSeconds;
    float m_ActualSeconds;
    float m_TimeWarp;
    float m_MaxDelta;

    // -dtsmooth: rolling mean of the raw frame delta.  See the definition of
    // SmoothDelta() for why this exists and why it is a mean.
    static const int MaxSmoothWindow = 32;
    float SmoothDelta(float delta);
    float m_SmoothRing[MaxSmoothWindow];
    int   m_SmoothNext;
    int   m_SmoothWindow;
    bool  m_SmoothChecked;
    unsigned m_FrameCount;

    // Windows timing high-resolution helper
    s64 m_StartCounter;
    s64 m_LastCounter;
    double m_Frequency;
};

class timeManagerProxy {
public:
    inline operator timeManager&() const { return *timeManager::sm_Instance; }
    inline timeManager* operator&() const { return timeManager::sm_Instance; }
    inline timeManager* operator->() const { return timeManager::sm_Instance; }

    inline void RealTime() const { timeManager::sm_Instance->RealTime(); }
    inline void FixedFrame(int fps) const { timeManager::sm_Instance->FixedFrame(fps); }
    inline void FixedFrame(float fps, bool allowOverrun) const { timeManager::sm_Instance->FixedFrame(fps, allowOverrun); }
    inline void Update() const { timeManager::sm_Instance->Update(); }
    inline void Reset() const { timeManager::sm_Instance->Reset(); }

    inline float GetElapsedTime() const { return timeManager::sm_Instance->GetElapsedTime(); }
    inline float GetSeconds() const { return timeManager::sm_Instance->GetSeconds(); }
    inline float GetInvSeconds() const { return timeManager::sm_Instance->GetInvSeconds(); }
    inline float GetUnwarpedSeconds() const { return timeManager::sm_Instance->GetUnwarpedSeconds(); }
    inline float GetActualSeconds() const { return timeManager::sm_Instance->GetActualSeconds(); }
    inline float GetUnwarpedRealtimeSeconds() const { return timeManager::sm_Instance->GetUnwarpedRealtimeSeconds(); }
    inline void SetSeconds(float s) const { timeManager::sm_Instance->SetSeconds(s); }

    inline float GetTimeWarp() const { return timeManager::sm_Instance->GetTimeWarp(); }
    inline void SetTimeWarp(float warp) const { timeManager::sm_Instance->SetTimeWarp(warp); }

    inline void SetClamp(float maxDelta) const { timeManager::sm_Instance->SetClamp(maxDelta); }
    inline void SetClamp(float minDelta, float maxDelta) const { timeManager::sm_Instance->SetClamp(minDelta, maxDelta); }
    inline float GetClamp() const { return timeManager::sm_Instance->GetClamp(); }

    inline unsigned GetFrameCount() const { return timeManager::sm_Instance->GetFrameCount(); }
    inline void SetElapsedTime(float t) const { timeManager::sm_Instance->SetElapsedTime(t); }
    inline void SetFrameCount(unsigned c) const { timeManager::sm_Instance->SetFrameCount(c); }
};

inline timeManagerProxy TIME;

// AGE 2.72 spelling: static access to the frame counter and the instance.
class datTimeManager {
public:
    static unsigned GetFrameCount() { return TIME.GetFrameCount(); }
    static timeManager &Get() { return TIME; }
};

class Timer {
public:
    Timer();
    void Reset();
    float Time() const;
    float MsTime() const { return Time() * 1000.0f; }
    // Block the calling thread for `ms` milliseconds.
    static void Sleep(int ms);

private:
    s64 m_StartCounter;
    double m_Frequency;
};

#endif // DATA_TIMEMGR_H
