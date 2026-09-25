#ifndef PTX_BASE_DRAW_H
#define PTX_BASE_DRAW_H

////////////////////////////////////////
// ptx_base/draw.h
//
// Particle drawing primitives.  The point-sprite structs and draw calls live
// in gfx/ptsprite.h; this header adds the immediate-mode particle quad path
// (ptxBegin/ptxDraw/ptxEnd) the software particle system (swptx) uses.
////////////////////////////////////////

#include "core/output.h"
#include "core/types.h"
#include "gfx/statetypes.h"
#include "gfx/rstate.h"
#include "gfx/texture.h"
#include "gfx/ptsprite.h"
#include "vector/vector3.h"
#include "vector/Vector4.h"

const int ptxBeginMax = 512;

// One buffered particle quad: atlas frame, rotation about the view axis,
// world position, half-extents and packed colour.  Filled with Init() and
// drawn in batches by ptxDrawArray (mc3 buffers glow reflections this way).
struct ptxDrawInfo {
    Vector3 pos;
    float scale;      // == width (kept for callers that treat quads as uniform)
    u32 color;
    float rot;        // radians
    float width;
    float height;
    int frame;        // atlas frame

    ptxDrawInfo() : pos(0.0f, 0.0f, 0.0f), scale(0.0f), color(0xFFFFFFFFu), rot(0.0f), width(0.0f), height(0.0f), frame(0) {}

    void Init(int frameNum, float rotation, float x, float y, float z, float h, float w, u32 c) {
        frame = frameNum; rot = rotation;
        pos.x = x; pos.y = y; pos.z = z;
        height = h; width = w; scale = w;
        color = c;
    }
};

// Draws `count` buffered quads as point sprites under the current texture
// and render state (ptx_base/draw.cpp).
void ptxDrawArray(const ptxDrawInfo *quads, int count);

#ifndef FIXCOLOR
#define FIXCOLOR(c) (c)
#endif

// Immediate particle quads.  A batch is opened with ptxBegin, filled with
// ptxDraw and submitted by ptxEnd; each quad faces the camera, is rotated
// about the view axis and has its own width and height, which is how rain
// draws long thin streaks and sparks draw short ones from the same call.
//
// Rain and sparks emit more quads than one batch holds, so they close and
// reopen a batch mid-loop; ptxBegin after ptxEnd simply starts the next one.

inline void ptxBuildRotTable() { }   // sprite rotation lookup: computed on the fly on PC, so there is no table to build
inline void ptxMicrocode(bool enable) { (void)enable; }   // uploads the VU1 particle microcode on PS2; no microcode on D3D

// Number of frames across the current texture atlas; ptxDraw's frameNum
// selects one of them.
void ptxSetRes(int res);
void ptxBegin(int count);
void ptxEnd();
void ptxDraw(int frameNum, float rot, float px, float py, float pz, float sx, float sy, u32 color);

// Raw quad path: four ptxVertex calls per quad, in world space with explicit
// texture coordinates (no camera facing).
void ptxBeginQuads(int count);
void ptxEndQuads();
void ptxVertex(float x, float y, float z, u32 color, float u, float v);

#endif // PTX_BASE_DRAW_H
