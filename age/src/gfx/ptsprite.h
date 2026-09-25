#ifndef GFX_PTSPRITE_H
#define GFX_PTSPRITE_H

////////////////////////////////////////
// gfx/ptsprite.h
//
// Point sprites: camera-facing textured quads drawn from position/scale,
// rotation/frame and colour arrays (particles, stars, sparks, glows).  The
// current texture (RSTATE.SetTexture) is an atlas of `texFrames` equal
// frames laid out horizontally; TexFrame selects one.
//
//   gfxPointSpriteBegin();
//   gfxPointSpriteDraw(frames, pos, rot, colors, count);   // colors optional
//   gfxPointSpriteEnd();
//
// The structs expose both spellings the game uses (x/y/z/scale and
// Position/Scale; rot/frame, Rotation/Frame and RotationAngle/TexFrame).
////////////////////////////////////////

#include "core/types.h"
#include "vector/vector3.h"

class gfxModel;   // AGE 2.72 ptsprite.h forward-declares it; mc3 exhaust flames rely on that

struct gfxPS_PosScale {
	union {
		struct { float x, y, z, scale; };
		struct { Vector3 Position; float Scale; };
	};
	u32 Color;      // packed RGBA (used when no colour array is given)

	gfxPS_PosScale() : x(0.0f), y(0.0f), z(0.0f), scale(0.0f), Color(0xFFFFFFFFu) {}
};

struct gfxPS_RotFrame {
	union {
		struct { float rot; int frame; };
		struct { float Rotation; int Frame; };
		struct { float RotationAngle; int TexFrame; };
	};

	gfxPS_RotFrame() : rot(0.0f), frame(0) {}
};

void gfxPointSpriteBegin();
void gfxPointSpriteEnd();

void gfxPointSpriteDraw(int texFrames, const gfxPS_PosScale *pos, const gfxPS_RotFrame *rot, const unsigned *colors, int count);
void gfxPointSpriteDraw(int texFrames, const gfxPS_PosScale *pos, const gfxPS_RotFrame *rot, int count);
inline void gfxPointSpriteDrawInline(int texFrames, const gfxPS_PosScale *pos, const gfxPS_RotFrame *rot, const unsigned *colors, int count)
	{ gfxPointSpriteDraw(texFrames, pos, rot, colors, count); }
inline void gfxPointSpriteDrawInline(int texFrames, const gfxPS_PosScale *pos, const gfxPS_RotFrame *rot, int count)
	{ gfxPointSpriteDraw(texFrames, pos, rot, count); }

// Fills rot[i].Rotation so each sprite's "up" follows its velocity on screen.
void gfxPointSpriteComputeRotations(const gfxPS_PosScale *pos, const Vector3 *velocities, gfxPS_RotFrame *rot, int count);

#endif // GFX_PTSPRITE_H
