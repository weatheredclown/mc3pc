#ifndef PROFILE_EKG_H
#define PROFILE_EKG_H

#include "profile/profiler.h"

class pfEKGMgr {
public:
    static void RegisterPage(pfPage* page);
    static pfPage* GetPage();
    static void CyclePage();
    static inline void Draw() { pfProfiler::Draw(); }
    static void AddWidgets(class bkBank &bank);
    static void NextPage() { CyclePage(); }
    static void ScaleDisplayUp() { sm_DisplayScale *= 2.0f; }
    static void ScaleDisplayDown() { sm_DisplayScale *= 0.5f; }
    static float GetDisplayScale() { return sm_DisplayScale; }
    static float sm_DisplayScale;
};

#define PF_SET(x, y) pfProfiler::RecordValue(#x, (float)(y))

#endif // PROFILE_EKG_H
