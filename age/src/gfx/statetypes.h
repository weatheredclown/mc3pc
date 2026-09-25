////////////////////////////////////////
// statetypes.h
////////////////////////////////////////

#ifndef GFX_STATETYPES_H
#define GFX_STATETYPES_H

enum gfxCullMode {
    cullNone = 0,
    cullCW = 1,
    cullCCW = 2,
    cullBack = cullCW,     // D3D default winding: CW faces away
    cullFront = cullCCW
};

enum gfxLightType {
    lightAmbient = 0,
    lightDirectional = 1,
    lightPoint = 2,
    lightSpot = 3
};

enum EnumFillMode { fillSolid, fillWire, fillPoint };

enum EnumBlendSet {
    blendSet_One_Zero = 0,
    blendSet_SrcAlpha_InvSrcAlpha = 1,
    blendSet_One_One = 2,
    blendSet_MinusOne_One = 3,
    blendSet_One_SrcAlpha = 4,
    blendSet_SrcAlpha_One = 5,
    blendSet_InvSrcAlpha_SrcAlpha = 6,
    blendSet_DestMinusSrc = 7,
    blendSet_DestColor_Zero = 8,
    blendSet_InvSrcAlpha_Zero = 9,
    blendSet_SrcAlpha_Zero = 10,
    blendSet_DestAlpha_One = 11,
    blendSet_DestAlpha_InvDestAlpha = 12
};
typedef EnumBlendSet gfxBlendSet;
typedef EnumBlendSet BlendSet;

enum EnumZFunc {
    zNever = 0,
    zLess = 1,
    zEqual = 2,
    zLEqual = 3,
    zGreater = 4,
    zNotEqual = 5,
    zGEqual = 6,
    zAlways = 7,

    zCloser = zLess,
    zCloserEqual = zLEqual,
    zLessEqual = zLEqual
};
typedef EnumZFunc gfxZFunc;

enum gfxAlphaFunc {
    alphaNever = 0,
    alphaLess = 1,
    alphaEqual = 2,
    alphaLEqual = 3,
    alphaGreater = 4,
    alphaNotEqual = 5,
    alphaGEqual = 6,
    alphaAlways = 7,

    // Compatibility names:
    atstNever = alphaNever,
    atstAlways = alphaAlways,
    atstEqual = alphaEqual,
    atstNotEqual = alphaNotEqual,
    atstGreater = alphaGreater,
    atstLess = alphaLess,
    atstGreaterEqual = alphaGEqual,
    atstLessEqual = alphaLEqual
};

enum gfxBlendMode {
    blendZero = 0,
    blendOne = 1,
    blendSrcColor = 2,
    blendInvSrcColor = 3,
    blendSrcAlpha = 4,
    blendInvSrcAlpha = 5,
    blendDestAlpha = 6,
    blendInvDestAlpha = 7,
    blendDestColor = 8,
    blendInvDestColor = 9,
    blendSrcAlphaSat = 10,
    
    // PS2 compatibility blend factors:
    blendCs = 11,
    blendCd = 12,
    blendAs = 13,
    blendAd = 14,
    blendFixed = 15
};
typedef gfxBlendMode gfxBlendFunc;

enum gfxTextureSource {
    texsrcTexCoord0 = 0,
    texsrcPassThru = texsrcTexCoord0,
    texsrcTexCoord1 = 1,
    texsrcTexCoord2 = 2,
    texsrcTexCoord3 = 3,
    texsrcTexCoord4 = 4,
    texsrcTexCoord5 = 5,
    texsrcTexCoord6 = 6,
    texsrcTexCoord7 = 7,
    texsrcCameraSpaceNormal = 8,
    texsrcCameraSpacePosition = 9,
    texsrcCameraSpaceReflectionVector = 10
};

enum gfxTextureOp {
    texopDisable = 1,
    texopSelectArg1 = 2,
    texopSelectArg2 = 3,
    texopModulate = 4,
    texopModulate2X = 5
};

enum gfxTextureArg {
    texargCurrent = 1,
    texargTexture = 2
};

// Alpha-test fail action (PS2 GS AFAIL): what a fragment that fails the
// alpha test may still write.  The D3D11 backend has one write mask per
// draw, so it maps afailFBOnly/afailRGBOnly to "pass, but no depth write"
// and afailZOnly to "pass, but no colour write" (see gfxRenderState::SetAlphaFail).
enum gfxAlphaFail {
    afailKeep = 0,      // discard the fragment entirely (D3D default)
    afailFBOnly = 1,    // colour only (no depth)
    afailZOnly = 2,     // depth only (no colour)
    afailRGBOnly = 3,   // RGB only (no alpha, no depth)
    afailZBOnly = afailZOnly
};

// Blend equation (gfxRenderState::SetBlendOp).
enum gfxBlendOp {
    blendOpAdd = 0,
    blendOpSubtract = 1,      // src - dest
    blendOpRevSubtract = 2,   // dest - src
    blendOpMin = 3,
    blendOpMax = 4
};

// Distance fog curve (gfxRenderState::SetFogMode).
enum gfxFogMode {
    fogNone = 0,
    fogLinear = 1,
    fogExp = 2,
    fogExp2 = 3
};

// Destination-alpha test (PS2 GS DATE).  Recorded by the render state; the
// backend has no per-pixel dest-alpha test.
enum gfxDestAlphaModes {
    destAlphaDisable = 0,
    destAlphaOnePass = 0,
    destAlphaEqualZero = 1,
    destAlphaEqualOne = 2
};

// Stencil operations (gfxRenderState::SetStencil)
enum gfxStencilOp {
    stencilopKeep = 0,
    stencilopZero,
    stencilopReplace,
    stencilopIncrSat,
    stencilopDecrSat,
    stencilopInvert,
    stencilopIncr,
    stencilopDecr
};

struct gfxStencilState {
    bool enable;
    EnumZFunc func;          // compare (ref & readMask) against the buffer
    int ref;
    unsigned readMask;
    unsigned writeMask;
    gfxStencilOp pass;       // stencil + depth pass
    gfxStencilOp fail;       // stencil fail
    gfxStencilOp zfail;      // stencil pass, depth fail

    gfxStencilState()
        : enable(false), func(zAlways), ref(0), readMask(0xff), writeMask(0xff),
          pass(stencilopKeep), fail(stencilopKeep), zfail(stencilopKeep) {}
};

// Sampler control (gfxRenderState::SetTextureAddress / SetTextureFilter)
enum gfxTexAddress { texaddrWrap = 0, texaddrClamp = 1, texaddrMirror = 2 };
enum gfxTexFilter { texfilterPoint = 0, texfilterLinear = 1, texfilterNone = 2 };

#endif // GFX_STATETYPES_H
