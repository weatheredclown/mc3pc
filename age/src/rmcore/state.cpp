#include "rmcore/state.h"
#include "vector/Matrix34.h"
#include "gfx/rstate.h"
#include "core/assert.h"

bool rmcEnableResourcedModel = false;

// Dirty flags/resets render pipeline state cache in RSTATE.
void rmcState::Dirty() {
    RSTATE.Default();
}

// PC port: the pass's blend set reaches the gfx layer.  rmcbsNormal is the
// usual texture-alpha blend; ONE/ZERO (the city's ground and main passes,
// rmcbsOverwrite) is opaque and turns the per-texture alpha blending off.
void rmcState::SetBlendSet(int set) {
    EnumBlendSet gfxSet = blendSet_SrcAlpha_InvSrcAlpha;
    if (set & rmcBlendSetCustomTag) {
        gfxSet = (EnumBlendSet)(set & ~rmcBlendSetCustomTag);
    } else {
        switch (set) {
            case rmcbsNormal:      gfxSet = blendSet_SrcAlpha_InvSrcAlpha; break;
            case rmcbsAdditive:    gfxSet = blendSet_SrcAlpha_One; break;
            case rmcbsSubtractive: gfxSet = blendSet_MinusOne_One; break;
            case rmcbsOverwrite:   gfxSet = blendSet_One_Zero; break;
            default:               gfxSet = blendSet_SrcAlpha_InvSrcAlpha; break;
        }
    }
    RSTATE.SetBlendSet(gfxSet);
    RSTATE.SetTextureAlphaBlendAllowed(gfxSet != blendSet_One_Zero);
}

void rmcState::SetAlphaBlend(bool enable) {
    RSTATE.SetAlphaBlendEnable(enable);
}

// The rm layer's world-matrix setter — rmwDrawable::Draw calls this before
// rmcDrawable::Draw(shaderGroup, ...), which renders under the current world.
void rmcState::SetWorldFast(const Matrix34 &m) {
    RSTATE.SetWorld(m);
}

// Sets the view camera for everything drawn afterwards (rm drawables and the
// vgl immediate path both read RSTATE's camera).
void rmcState::SetCamera(const Matrix34 &m) {
    RSTATE.SetCamera(m);
}

// SetState sets rendering pipeline states (e.g., rmcsForceColor override).
void rmcState::SetState(int state, unsigned long val) {
    if (state == rmcsForceColor) {
        RSTATE.SetForceColor(val);
    } else {
        Assert(0);
    }
}

// SetBestLighting chooses best lighting configuration based on lights (0=off, 1=ambient+dir, 2=point).
void rmcState::SetBestLighting(int val) {
    RSTATE.SetLightingMode(val);
}

// SetLighting sets light source bindings.
void rmcState::SetLighting(const class rmcLightGroup &lights) {
    RSTATE.SetLighting(true);
    RSTATE.SetLights(lights);
}

// SetBestFog chooses best fog mode.
void rmcState::SetWorstCull(int mode) {
    RSTATE.SetCull((gfxCullMode)mode);
}

void rmcState::SetBestFog(int val) {
    RSTATE.SetFogEnable(val != 0);
}

// SetFogParams configures distance/color parameters for fog effects.
void rmcState::SetFogParams(unsigned long color, float start, float end, float min, float max) {
    RSTATE.SetFogEnable(true);
    RSTATE.SetFogParams(color, start, end, min, max);
}

void rmcState::SetFogPower(float power) {
    RSTATE.SetFogPower(power);
}

void rmcState::SetCullMode(int mode) {
    switch (mode) {
        case rmccmNone:  RSTATE.SetCull(cullNone); break;
        case rmccmFront: RSTATE.SetCull(cullFront); break;
        case rmccmBack:  RSTATE.SetCull(cullBack); break;
        default:         RSTATE.SetCull(cullBack); break;
    }
}

static unsigned long s_DefaultStates[64] = {0};

unsigned long rmcState::GetState(int state) {
    if (state >= 0 && state < 64) {
        return s_DefaultStates[state];
    }
    return 0;
}

void rmcState::SetColorWrite(int mode) {
    switch (mode) {
        case rmccwNone:  RSTATE.SetColorMask(false, false); break;
        case rmccwRGB:   RSTATE.SetColorMask(true, false); break;
        case rmccwRGBA:  RSTATE.SetColorMask(true, true); break;
        case rmccwAlpha: RSTATE.SetColorMask(false, true); break;
        default:         RSTATE.SetColorMask(true, true); break;
    }
}

void rmcState::SetFailAlpha(int mode) {
    switch (mode) {
        case rmcfaKeep:    RSTATE.SetAlphaFail(afailKeep); break;
        case rmcfaDrop:    RSTATE.SetAlphaFail(afailZOnly); break;
        case rmcfaRgbOnly: RSTATE.SetAlphaFail(afailRGBOnly); break;
        default:           RSTATE.SetAlphaFail(afailKeep); break;
    }
}

void rmcState::SetDestAlphaTest(int mode) {
    RSTATE.SetDestAlphaEnable(mode != rmcdaDisable);
    RSTATE.SetDestAlphaMode(mode);
}

void rmcState::SetTextureMatrix(int stage, const Matrix44 *m) {
    if (m) {
        RSTATE.SetTexMatrix(stage, *m);
    } else {
        static Matrix44 ident(Matrix44::I);
        RSTATE.SetTexMatrix(stage, ident);
    }
}

void rmcState::SetTextureCombine(int stage, int op) {
    if (op == rmctcModulate2x) {
        RSTATE.SetTexColorOp(stage, texopModulate2X);
    } else {
        RSTATE.SetTexColorOp(stage, texopModulate);
    }
}

void rmcState::SetTexture(int stage, class rmcTexture *tex) {
    RSTATE.SetTexture(stage, tex ? tex->GetGfxTexture() : nullptr);
}

void rmcState::SetDefaultState(int state, unsigned long val) {
    if (state >= 0 && state < 64) {
        s_DefaultStates[state] = val;
    }
    switch (state) {
        case rmcsCullMode:    SetCullMode((int)val); break;
        case rmcsLighting:    SetLightingMode((int)val); break;
        case rmcsFogBlend:    SetFogBlend(val != 0); break;
        case rmcsBlendSet:    SetBlendSet((int)val); break;
        case rmcsAlphaFunc:   SetAlphaFunc((int)val); break;
        case rmcsAlphaRef:    SetAlphaRef((int)val); break;
        case rmcsDepthFunc:   SetDepthFunc((int)val); break;
        case rmcsDepthWrite:  SetDepthWrite(val != 0); break;
        case rmcsSmoothShade: SetSmoothShade(val != 0); break;
        case rmcsAlphaBlend:  SetAlphaBlend(val != 0); break;
        case rmcsForceColor:  RSTATE.SetForceColor(val); break;
        case rmcsBaseColor:   RSTATE.SetBaseColor(val); break;
        case rmcsFillMode:
            if (val == rmcfmWire || val == rmcfmWireframe) RSTATE.SetFillMode(fillWire);
            else if (val == rmcfmPoint) RSTATE.SetFillMode(fillPoint);
            else RSTATE.SetFillMode(fillSolid);
            break;
        case rmcsColorWrite:    SetColorWrite((int)val); break;
        case rmcsDestAlphaTest: SetDestAlphaTest((int)val); break;
        case rmcsFailAlpha:     SetFailAlpha((int)val); break;
        default: break;
    }
}
