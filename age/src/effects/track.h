#ifndef EFFECTS_TRACK_H
#define EFFECTS_TRACK_H

#include "core/output.h"
#include "core/types.h"
#include "atl/array.h"
#include "vector/matrix34.h"
#include "vector/vector3.h"

class gfxTexture;

// fxTrack
//
// The skid mark left by one wheel: a ribbon of quads laid along the ground
// behind the contact point.  Update() is called every frame with the current
// contact point, the surface normal and which texture to lay (0 = nothing, so
// the ribbon breaks whenever the tyre stops sliding).
//
// The ribbon is a ring buffer of `trackSize` points (the game passes a power
// of two).  When it wraps, the oldest point is overwritten, which is what
// makes a track fade out behind a car without any per-point ageing.
class fxTrack {
public:
    fxTrack();
    virtual ~fxTrack();

    // mtx places the first point, tireWidth sets the ribbon width and
    // trackSize is how many points the ring holds.
    void Init(const Matrix34 &mtx, float tireWidth, int trackSize);
    void Reset();
    void SetWidth(float w)      { m_Width = w; }
    float GetWidth() const      { return m_Width; }

    // No contact information: ends the current run of marks.
    void Update();
    // Extend the ribbon at pos with surface up vector up.
    void Update(const Vector3 &pos, const Vector3 &up, gfxTexture *tex);
    // Same, choosing one of the shared skid textures by index (0 = no mark).
    void Update(const Vector3 &pos, const Vector3 &up, int texIndex);

    void Draw();

    // The skid textures every track shares.  Loaded once per level.
    static void InitStaticData();
    static void UnloadStaticData();
    static gfxTexture *GetSkidTexture(int index);

private:
    struct Point {
        Vector3 Left;
        Vector3 Right;
        bool Break;      // no mark between this point and the previous one
    };

    void AddPoint(const Vector3 &pos, const Vector3 &up, bool breakRun);

    atArray<Point> m_Points;
    int m_Head;              // next slot to write
    int m_Count;             // points held, up to m_Points.GetCount()
    float m_Width;
    Vector3 m_LastPos;
    bool m_HasLast;
    gfxTexture *m_Texture;
};

// fxTrackMgr
//
// Keeps the live tracks so the level can draw every skid mark in one pass,
// after the road and before the transparent effects.
class fxTrackMgr {
public:
    fxTrackMgr(int count = 128);
    virtual ~fxTrackMgr();

    // The texture load phase skid textures belong to.  The alpha keeps it in a
    // static, fxTrackMgr::sm_TexturePhase (0x5d48b0), set to 3 - the PS2 MFIFO
    // phase, the same value swPtxBirth::TexturePhase carries under USE_MFIFO.
    // gfxSetTextureLoadPhase is inert bookkeeping on PC (it stores a number and
    // hands back the previous one; nothing reads it), so this follows the
    // sibling convention in swptx/birth.cpp and uses 0 off the console.
    static int GetTexturePhase() { return sm_TexturePhase; }
    static void SetTexturePhase(int phase) { sm_TexturePhase = phase; }

    void Update();
    void Draw();

    void Register(fxTrack *track);
    void Unregister(fxTrack *track);

    static fxTrackMgr *sm_Instance;

private:
    static int sm_TexturePhase;
    atArray<fxTrack*> m_Tracks;
};

#define FXTRACKS fxTrackMgr::sm_Instance

#endif // EFFECTS_TRACK_H
