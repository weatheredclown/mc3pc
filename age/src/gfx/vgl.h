////////////////////////////////////////
// vgl.h
//
// Immediate-mode geometry submission (vgl* + rgl*), implemented on the D3D11
// dynamic vertex-buffer backend in gfx/rgl.cpp.
////////////////////////////////////////

#ifndef GFX_VGL_H
#define GFX_VGL_H

#include "core/output.h"
#include "core/types.h"
#include "vector/vector3.h"
#include "vector/vector2.h"
#include "vector/Vector4.h"
#include "vector/Matrix34.h"
#include "gfx/misc.h"		// gfxColor (float RGBA class) + gfxPackedColor (u32)

////////////////////////////////////////////////////////////////////////////
// Draw primitive types + colour

enum EnumDrawType
{
	drawPoints,
	drawLine,
	drawLineStrip,
	drawTri,
	drawTriangles = drawTri,
	drawTriStrip,
	drawTriFan,
	drawQuads,
};

// gfxColor (float RGBA class) + gfxPackedColor (u32) come from gfx/misc.h.
// The mk* helpers build PACKED colours (vertex / immediate-mode colours).

extern class gfxTexture* NoTexture;
extern bool g_Allow8BitImages;

inline gfxPackedColor mkfrgb(float r,float g,float b)
{
	u32 R=(u32)(r*255.0f), G=(u32)(g*255.0f), B=(u32)(b*255.0f);
	return 0xff000000u|(R<<16)|(G<<8)|B;
}
inline gfxPackedColor mkfrgb(const class Vector3 &c)
{
	return mkfrgb(c.x, c.y, c.z);
}
inline gfxPackedColor mkfrgba(float r,float g,float b,float a)
{
	u32 A=(u32)(a*255.0f);
	return (mkfrgb(r,g,b)&0x00ffffffu)|(A<<24);
}
inline gfxPackedColor mkfrgba(const class Vector4 &c)
{
	return mkfrgba(c.x, c.y, c.z, c.w);
}
inline gfxPackedColor mkrgb(u8 r, u8 g, u8 b)
{
	return 0xff000000u | (r << 16) | (g << 8) | b;
}
inline gfxPackedColor mkrgba(u8 r, u8 g, u8 b, u8 a)
{
	return (a << 24) | (r << 16) | (g << 8) | b;
}

void vglColor(gfxPackedColor c);
inline void vglColor4f(float r, float g, float b, float a) { vglColor(mkfrgba(r, g, b, a)); }
inline void vglColor4f(const class Vector4 &c) { vglColor(mkfrgba(c.x, c.y, c.z, c.w)); }
inline void vglColor3f(float r, float g, float b) { vglColor(mkfrgb(r, g, b)); }
inline void vglColor3f(const class Vector3 &c) { vglColor(mkfrgb(c.x, c.y, c.z)); }

extern const class Vector3 Color_white;
extern const class Vector3 Color_black;
extern const class Vector3 Color_red;
extern const class Vector3 Color_green;
extern const class Vector3 Color_blue;
extern const class Vector3 Color_yellow;
extern const class Vector3 Color_magenta;
extern const class Vector3 Color_cyan;
extern const class Vector3 Color_red2;
extern const class Vector3 Color_yellow3;

////////////////////////////////////////////////////////////////////////////
// rgl* - world-space immediate mode (grid / axes)

enum EnumRglCap
{
	RGL_LIGHTING,
	RGL_TEXTURE_2D
};

void rglWorldMatrix(const Matrix34 &m);
inline void rglWorldIdentity() { rglWorldMatrix(Matrix34::I); }
void rglBegin(EnumDrawType prim,int vertexCount);
void rglColor3f(float r,float g,float b);
void rglVertex3f(float x,float y,float z);
inline void rglVertex3f(const Vector3 &v) { rglVertex3f(v.x, v.y, v.z); }
void rglEnd();
// Every rglEnd/vglEnd submits its batch immediately; nothing is queued.
inline void rglFlush() { Quitf("rglFlush - not implemented"); }

// Capability toggles: RGL_LIGHTING drives RSTATE lighting, RGL_TEXTURE_2D the
// texture-enable (off = vertex colour only).  (rgl.cpp)
bool rglIsEnabled(EnumRglCap cap);
void rglEnableDisable(EnumRglCap cap, bool enable);
inline void rglEnable(EnumRglCap cap) { rglEnableDisable(cap, true); }
inline void rglDisable(EnumRglCap cap) { rglEnableDisable(cap, false); }
void vglBindTexture(class gfxTexture *tex);
inline void rglBindTexture(class gfxTexture *tex) { vglBindTexture(tex); }
void vglBindTexture2(class gfxTexture *tex);
void vglTex2Combine(int mode);   // 0 modulate, 1 decal (lerp by stage-2 alpha, alpha kept), 2 add stage-2 colour * stage-1 texture alpha (alpha kept), 3 decal of the vertex-coloured stage-2 texel (alpha kept); reset by vglBindTexture2
// Separate colour / alpha combine for stage 2 (the D3D8 stage-1 SELECTARG /
// MODULATE vocabulary the game's PC paths use): the colour op picks what the
// RGB becomes, the alpha op what the alpha becomes.
// tex2colAddByBaseAlpha: rgb = current + stage-2 rgb * stage-1 texture alpha (PS2 "Cs 0 Ad Cd").
// tex2colDecalVertexColor: rgb = lerp(current, vertex colour * stage-2 rgb, stage-2 alpha).
// tex2colAddByVertexAlpha: rgb = current + stage-2 rgb * VERTEX alpha.  Car paint
// reflecting the live city environment map: the per-vertex shading works out how
// reflective the surface is at that angle (fresnel * the material's reflectivity)
// and leaves it in the vertex alpha, so the reflection itself lands per pixel.
enum EnumTex2ColorOp { tex2colModulate = 0, tex2colDecal = 1, tex2colCurrent = 2, tex2colTexture = 3, tex2colAddByBaseAlpha = 4, tex2colDecalVertexColor = 5, tex2colAddByVertexAlpha = 6 };
enum EnumTex2AlphaOp { tex2alphaModulate = 0, tex2alphaCurrent = 1, tex2alphaTexture = 2 };
void vglTex2CombineOps(int colorOp, int alphaOp);
class gfxTexture *vglGetTexture2();
inline void rglBindTexture2(class gfxTexture *tex) { vglBindTexture2(tex); }
// PC PORT: per-pixel car bodywork (gfx/model.cpp sCarShade, evaluated in the pixel
// shader).  The consoles drew carpaint through textures sampled per pixel - the paint
// ramp through reflection texgen and a specular map holding one sprite per light
// (mcCarMetallicPaint::GenSpecular) - so its gradients and highlights never depended
// on the long panel triangles.  While set, the vertex colour carries only the baked
// occlusion (rgb) and the shader works out the ramp, diffuse, highlights and fresnel
// per pixel, leaving the reflection weight in the vertex-colour alpha for
// tex2colAddByVertexAlpha, whose environment lookup then also moves per pixel.
// All vectors are world space.  NULL turns it off.
struct vglCarShadeParams {
    float misc[4];          // enable (1), spec strength, gloss exponent, reflectivity
    float base[4];          // base colour rgb (no ramp), base alpha
    float cam[4];           // camera position, ambient / environment scale
    float key[4];           // direction towards the key light, key scale
    float misc2[4];         // highlight light count, ramp stop count (< 2: none), debug view (-carshadedebug: 1 normals, 2 occlusion), unused
    float specDir[3][4];    // towards the light (w 0) or its position (w 1)
    float specCol[3][4];    // highlight colour rgb
    float ramp[8][4];       // paint ramp stops: rgb, position 0..1
    // Glass (1) takes the stage-2 environment itself and writes its own alpha (fresnel
    // + highlight) instead of the reflection weight; lens light level 0..1; the alpha
    // added at full fresnel (car_glass_fres); unused.
    float glass[4];
};
void vglSetCarShade(const vglCarShadeParams *params);
inline void rglColor(gfxPackedColor c) { vglColor(c); }

////////////////////////////////////////////////////////////////////////////
// vgl* - current-state immediate mode (bones / overlays)

void vglBindTexture(gfxTexture *tex);
void vglTexCoord2f(float u,float v);
inline void vglTexCoord2f(const Vector2 &v) { vglTexCoord2f(v.x, v.y); }
// Second texture stage (lightmap/detail): bound texture modulates stage 1;
// unbound = white = no-op.
void vglTexCoord2f2(float u,float v);
// Model-space vertex normal for the lit path (transformed by gWorld in the VS).
void vglNormal3f(float x, float y, float z);
inline void vglNormal3f(const class Vector3 &n) { vglNormal3f(n.x, n.y, n.z); }
inline void vglTexCoord2f2(const Vector2 &v) { vglTexCoord2f2(v.x, v.y); }
void vglColor(gfxPackedColor c);
inline void vglColorUnFixed(gfxPackedColor c) { vglColor(c); }
void vglBegin(EnumDrawType prim,int vertexCount);
static const int vglBeginMax = 4096;
void vglVertex3f(const Vector3 &v);
void vglVertex3f(float x,float y,float z);
inline void vglVertex2i(int x, int y) { vglVertex3f((float)x, (float)y, 0.0f); }
void vglEnd();
#define vglVertex vglVertex3f

#ifndef DEVCOLOR
#define DEVCOLOR(c) ((gfxPackedColor)(c))
#endif

// World -> screen pixel under the live camera + viewport projection; false
// when the point is behind the camera.  (rgl.cpp)
bool vglProject(const class Vector3 &world, float &screenX, float &screenY);
// Screen pixel -> the world-space points on the near and far planes (the
// mouse pick ray), via the inverse view*projection.  (rgl.cpp)
void vglComputePickRay(float screenX, float screenY, class Vector3 &nearPt, class Vector3 &farPt);
// Debug text projected at a world position (editor node labels).  (rgl.cpp)
void vglDrawLabelf(const class Vector3 &pos, const char *fmt, ...);
void vglDrawLabelf(const class Vector3 &pos, int dx, int dy, const char *fmt, ...);
// Non-formatting forms (AGE 2.72): dx/dy are offsets in font cells, so a
// caller can stack several labels under one world point.  (rgl.cpp)
void vglDrawLabel(const class Vector3 &pos, const char *text);
void vglDrawLabel(const class Vector3 &pos, int dx, int dy, const char *text);

enum vglProjectStatus {
    vglProjectOK,
    vglProjectBehind,
    vglProjectClip,
    vglProjectVisible = vglProjectOK,
    vglProjectOffscreen = vglProjectClip
};

enum EnumRenderMode {
    renderWireframe = 0,
    renderSolid = 1
};
extern EnumRenderMode ageRenderMode;

// tgl* — the PS2 batched quad path (particles, sprites).  On PC every
// tglEnd submits directly, so a batch has no close-out work: tglBeginBatch
// only resets the texture-coordinate generation the batched draws bypass.
void tglColor4fImpl(float r, float g, float b, float a);
void vglResetTexGen();   // drop any shader-stage texture matrix (the batched paths draw raw UVs)
inline void tglBeginBatch() { vglResetTexGen(); }
inline void tglEndBatch() { Quitf("tglEndBatch - not implemented"); }
inline void tglBindTexture(class gfxTexture *tex) { vglBindTexture(tex); }
inline void tglBegin(EnumDrawType prim, int count) { vglBegin(prim, count); }
inline void tglColor4f(const class Vector4 &c) { tglColor4fImpl(c.x, c.y, c.z, c.w); }
inline void tglTexCoord2f(float u, float v) { vglTexCoord2f(u, v); }
inline void vglVertexUnFixed(float x, float y, float z, gfxPackedColor color, float u, float v) {
    vglColor(color);
    vglTexCoord2f(u, v);
    vglVertex3f(x, y, z);
}
inline void tglVertex3f(float x, float y, float z) { vglVertex3f(x, y, z); }
inline void tglVertex3f(const class Vector3 &v) { vglVertex3f(v); }
inline void tglEnd() { vglEnd(); }

#endif // GFX_VGL_H
