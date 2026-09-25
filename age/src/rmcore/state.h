#ifndef RMCORE_STATE_H
#define RMCORE_STATE_H

#include "core/output.h"
#include "core/types.h"
#include "rmcore/texture.h"
#include "rmcore/shader.h"
#include "rmcore/light.h"
#include "gfx/statetypes.h"
#include "gfx/rstate.h"

class Matrix34;

const int rmcTexStageCount = 4;

// Custom blend sets are named with the D3D8 blend-factor vocabulary
// (gfx/d3dcompat.h) and resolve to the engine's EnumBlendSet.
#include "gfx/d3dcompat.h"

// Tagged so rmcState::SetBlendSet can tell an engine EnumBlendSet (which starts
// at blendSet_One_Zero = 0) from the rmcBlendSet names below (rmcbsNormal = 0).
const int rmcBlendSetCustomTag = 0x1000;
inline int rmcBlendSetCustom_D3D(int src, int dst) { return rmcBlendSetCustomTag | (int)gfxD3DBlendSetFromPair((DWORD)src, (DWORD)dst); }

enum rmcStateConstant {
    rmcsForceColor = 1,
    rmcsBaseColor,
    rmcsCullMode,
    rmcsLighting,
    rmcsFogBlend,
    rmcsBlendSet,
    rmcsAlphaFunc,
    rmcsAlphaRef,
    rmcsDepthFunc,
    rmcsDepthWrite,
    rmcsSmoothShade,
    rmcsFillMode,
    rmcsColorWrite,
    rmcsDestAlphaTest,
    rmcsAlphaBlend,
    rmcsFailAlpha,
    rmcsWorstCull,
    rmcsBestLighting,
    rmcsBestFog
};

enum rmcCullMode {
    rmccmNone = 0,
    rmccmFront = 1,
    rmccmBack = 2
};

const int rmcrtPermanent = 1;
enum rmctcCombine { rmctcModulate = 0, rmctcModulate2x = 1, rmctcSelectArg1 = 2 };
enum rmctgGen { rmctgPassThru0 = 0, rmctgPassThru1 = 1 };

extern bool rmcEnableResourcedModel;
inline void rmcSetCpvMode(bool enable = true) { (void)enable; }

enum rmcLightingMode {
    rmclmNone = 0,
    rmclmVertex = 1,
    rmclmSpecular = 2,
    rmclmDirectional = 3,
    rmclmPoint = 4
};

enum rmcBlendSet {
    rmcbsNormal = 0,
    rmcbsAdditive = 1,
    rmcbsSubtractive = 2,
    rmcbsAdd = 1,
    rmcbsSub = 2,
    rmcbsOverwrite = 100
};

enum rmcAlphaFunc {
    rmcafGreaterEqual = 7,
    rmcafNever = 0,
    rmcafLess = 1,
    rmcafEqual = 2,
    rmcafLEqual = 3,
    rmcafGreater = 4,
    rmcafNotEqual = 5,
    rmcafGEqual = 6,
    rmcafAlways = 7
};

enum rmcDepthFunc {
    rmcdfNever = 0,
    rmcdfLess = 1,
    rmcdfEqual = 2,
    rmcdfLEqual = 3,
    rmcdfGreater = 4,
    rmcdfNotEqual = 5,
    rmcdfGEqual = 6,
    rmcdfAlways = 7,
    rmcdfCloser = 1,
    rmcdfCloserEqual = 3
};

enum rmcFillMode {
    rmcfmSolid = 0,
    rmcfmWire = 1,
    rmcfmPoint = 2,
    rmcfmWireframe = 1
};

enum rmcColorWrite {
    rmccwNone = 0,
    rmccwRGB = 1,
    rmccwRGBA = 2,
    rmccwAlpha = 3
};

// Destination-alpha test.  The frame's alpha channel is a mask the renderer
// writes on purpose (reflection strength, HDR bloom), and the console could
// test it per pixel.  D3D11 has no such test; these are recorded
// (gfxRenderState::SetDestAlphaMode) but not enforced, so a pass that relies
// on one to MASK its output has to be written not to need it.
enum rmcDestAlphaTest {
    rmcdaDisable = 0,
    rmcdaEnable = 1,
    rmcdaEqualOne = 2,
    rmcdaEqualZero = 3
};

enum rmcFailAlpha {
    rmcfaKeep = 0,
    rmcfaDrop = 1,
    rmcfaRgbOnly = 2
};

class rmcState {
public:
    static void Dirty();
    static void SetWorld(const Matrix34 &m) { RSTATE.SetWorld(m); }
    static void SetWorldFast(const Matrix34 &m);
    static void SetState(int state, unsigned long val);
    static unsigned long GetState(int state = 0);
    // Texture bound on `stage` (stage 0 only on the D3D11 backend).
    static const class gfxTexture *GetTexture(int stage = 0) { return stage == 0 ? RSTATE.GetTexture() : nullptr; }
    static int GetCullMode() { return (int)RSTATE.GetCull(); }
    static void SetDefaultState(int state, unsigned long val = 0);
    static void SetColorWrite(int mode);
    static void SetFailAlpha(int mode);
    static void SetDestAlphaTest(int mode);
    static void SetForceColor(unsigned long color) { RSTATE.SetForceColor(color); }
    static void SetForceColor(const gfxColor &color) { RSTATE.SetForceColor(color); }
    static void SetForceColor(float r, float g, float b, float a = 1.0f) { RSTATE.SetForceColor(gfxColor(r, g, b, a)); }
    // Camera: forwarded to the gfx state (RSTATE) so rm and immediate-mode
    // drawing share one view; SetCamera also invalidates any cached lighting.
    static void SetCamera(const Matrix34 &m);
    static const Vector3 &GetCameraPosition() { return RSTATE.GetCameraPosition(); }
    static const Matrix34 &GetCamera() { return RSTATE.GetCamera(); }
    static const Matrix34 &GetWorld() { static Matrix34 s_World(Matrix34::I); return s_World; }
    static int GetAlphaRef() { return RSTATE.GetAlphaRef(); }

    static void SetWorstCull(int mode);
    static void SetBestLighting(int val);
    static void SetBestLightingMode(int mode) { RSTATE.SetLightingMode(mode); }
    static void SetLightingMode(int mode) { RSTATE.SetLightingMode(mode); }
    static int GetLightingMode() { return RSTATE.GetLightingMode(); }
    static void SetLighting(const class rmcLightGroup &lights);
    static void SetLightingGroup(const class rmcLightGroup *lg) { if (lg) SetLighting(*lg); }
    static void SetLightingGroup(const class rmcLightGroup &lg) { SetLighting(lg); }
    static const class rmcLightGroup *GetLightingGroup() {
        static class rmcLightGroup s_lg;
        return &s_lg;
    }
    static void SetBestFog(int val);
    static void SetFogBlend(bool enable) { RSTATE.SetFogEnable(enable); }
    static void SetFogParams(unsigned long color, float start, float end, float min, float max);
    static void SetFogPower(float power);

    static void SetBaseColor(unsigned long color) { RSTATE.SetBaseColor(color); }
    static void SetBaseColor(const gfxColor &c) { RSTATE.SetBaseColor(c); }
    static void SetBaseColor(float r, float g, float b, float a = 1.0f) { RSTATE.SetBaseColor(r, g, b, a); }
    // PC port: real state (state.cpp).  An rmcBlendSet name or a tagged
    // rmcBlendSetCustom_D3D() value; an opaque set (ONE/ZERO, rmcbsOverwrite)
    // also stops gfxModel from blending textures that carry alpha - the city's
    // ground and main passes draw opaque road textures whose CLUT alpha is low.
    static void SetBlendSet(int set);
    static void SetAlphaBlend(bool enable);
    static bool GetAlphaBlend() { return RSTATE.GetAlphaBlendEnable(); }
    static void SetAlphaRef(int ref) { RSTATE.SetAlphaRef(ref); }
    static void SetAlphaFunc(int func) { RSTATE.SetAlphaFunc(func); }
    static rmcAlphaFunc GetAlphaFunc() { return (rmcAlphaFunc)RSTATE.GetAlphaFunc(); }
    static void SetDepthFunc(int func) { RSTATE.SetZFunc((EnumZFunc)func); }
    static void SetDepthTest(bool enable) { RSTATE.SetZTestEnable(enable); }
    static bool GetDepthTest() { return RSTATE.GetZTestEnable(); }
    static void SetDepthWrite(bool enable) { RSTATE.SetZWriteEnable(enable); }
    static void SetSmoothShade(bool enable) { (void)enable; }
    static void SetCullMode(int mode);
    static void SetTextureMatrix(int stage, const Matrix34 &m) { RSTATE.SetTextureMatrix(stage, m); }
    static void SetTextureMatrix(int stage, const Matrix34 *m) {
        if (m) RSTATE.SetTextureMatrix(stage, *m);
        else { static Matrix34 ident(Matrix34::I); RSTATE.SetTextureMatrix(stage, ident); }
    }
    static void SetTextureMatrix(int stage, const class Matrix44 *m);
    static void SetTextureMatrix(int stage, int nullVal) { (void)nullVal; static Matrix34 ident(Matrix34::I); RSTATE.SetTextureMatrix(stage, ident); }
    static void SetTextureMatrix(const Matrix34 &m) { SetTextureMatrix(0, m); }
    static void SetTextureMatrix(const Matrix34 *m) { SetTextureMatrix(0, m); }
    static void SetTextureMatrix(const class Matrix44 *m) { SetTextureMatrix(0, m); }
    static void SetTextureMatrix(int nullVal) { SetTextureMatrix(0, nullVal); }
    static void SetTextureCombine(int stage, int op);
    static void SetTextureCombine(int stage, int op, int arg1, int arg2) { (void)arg1; (void)arg2; SetTextureCombine(stage, op); }
    static void SetTextureCombine(int op, int arg1, int arg2) { SetTextureCombine(0, op, arg1, arg2); }
    static void SetTextureGeneration(int stage, int gen) { RSTATE.SetTexGeneration(stage, 0, false, gen, 0); }
    static void SetTextureGeneration(int gen) { SetTextureGeneration(0, gen); }
    // Nothing is deferred on this backend; see gfxRenderState::Flush.
    static void Flush() { }
    static void FlushTexture(const char *name = nullptr) { (void)name; }
    static void FlushTexture(rmcTexture *tex) { SetTexture(0, tex); }
    static void SetTexture(int stage, class rmcTexture *tex);
    static void SetTexture(int stage, class gfxTexture *tex) { RSTATE.SetTexture(stage, tex); }
    static void SetTexture(class rmcTexture *tex) { SetTexture(0, tex); }
    static void SetTexture(class gfxTexture *tex) { SetTexture(0, tex); }
};

#endif // RMCORE_STATE_H
