////////////////////////////////////////
// isect.h
////////////////////////////////////////

#ifndef PHCORE_ISECT_H
#define PHCORE_ISECT_H

#include "core/output.h"
#include "vector/vector3.h"

#ifndef NULL
#define NULL 0
#endif

class phInst;

class phIntersection {
public:
    Vector3 Position;
    Vector3 Normal;
    phInst* PhysInst;
    int PartIndex;
    int Component;
    float Depth;
    union {
        float TVal;   // parametric position along the probe segment (0..1+); set by TestEdge/Set
        float tVal;
    };
    bool  Hit;    // explicit hit flag from Set(); PhysInst is the dominant "is a hit" marker

    phIntersection()
        : Position(0,0,0), Normal(0,0,0), PhysInst(NULL),
          PartIndex(0), Component(0), Depth(0.0f), TVal(0.0f), Hit(false) {}

    bool IsAHit() const { return PhysInst != NULL; }

    // Fill from a probe result: (point, surface normal, segment-T, penetration, hit, part index).
    void Set(const Vector3 &pos, const Vector3 &normal, float t, float depth, bool hit, int index) {
        Position = pos; Normal = normal; TVal = t; Depth = depth; Hit = hit; PartIndex = index;
    }
};



#ifdef WEAPONS_SHELL_H
#ifndef WEAP_SHELL_HACK
#define WEAP_SHELL_HACK
class weapShell_Hack : public weapShell {
public:
    void Load() {
        weapShell::Load(nullptr);
    }
    void Init() { Quitf("weapShell_Hack::Init - not implemented"); }
};
#endif

#undef WEAPSHELL
#define WEAPSHELL (*(weapShell_Hack*)&g_WeapShell)
#endif

#ifdef PARTICLE_MANAGER_H
#ifndef PTX_MANAGER_HACK
#define PTX_MANAGER_HACK
class ptxManager_Hack : public ptxManager {
public:
    static void InitClass() {
        ptxManager::InitClass(nullptr);
    }
};
#define ptxManager ptxManager_Hack
#endif
#endif

#define SetEntityTypeManager SetEntityTypeManager_Hacked

#endif // PHCORE_ISECT_H
