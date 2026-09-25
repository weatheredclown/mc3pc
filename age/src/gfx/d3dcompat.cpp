#include "gfx/d3dcompat.h"

#include "core/output.h"
#include "gfx/rstate.h"
#include "gfx/simple.h"
#include "gfx/statetypes.h"
#include "gfx/texture.h"
#include "gfx/vgl.h"

#include <string.h>

static gfxD3DCompatDevice sCompatDevice;
gfxD3DCompatDevice *lpD3DDev = &sCompatDevice;
DWORD gfxPixelShaders = 0;
// gfxHardwareShaders lives in rgl.cpp (the D3D11 backend always has them).
bool gfxDisableNextStage = false;

////////////////////////////////////////////////////////////////////////////////
// Mapping helpers

static EnumZFunc sMapCmp(DWORD f)
{
	switch (f) {
	case D3DCMP_NEVER: return zNever;
	case D3DCMP_LESS: return zLess;
	case D3DCMP_EQUAL: return zEqual;
	case D3DCMP_LESSEQUAL: return zLEqual;
	case D3DCMP_GREATER: return zGreater;
	case D3DCMP_NOTEQUAL: return zNotEqual;
	case D3DCMP_GREATEREQUAL: return zGEqual;
	default: return zAlways;
	}
}

static int sMapAlphaCmp(DWORD f)
{
	switch (f) {
	case D3DCMP_NEVER: return alphaNever;
	case D3DCMP_LESS: return alphaLess;
	case D3DCMP_EQUAL: return alphaEqual;
	case D3DCMP_LESSEQUAL: return alphaLEqual;
	case D3DCMP_GREATER: return alphaGreater;
	case D3DCMP_NOTEQUAL: return alphaNotEqual;
	case D3DCMP_GREATEREQUAL: return alphaGEqual;
	default: return alphaAlways;
	}
}

static gfxStencilOp sMapStencilOp(DWORD op)
{
	switch (op) {
	case D3DSTENCILOP_ZERO: return stencilopZero;
	case D3DSTENCILOP_REPLACE: return stencilopReplace;
	case D3DSTENCILOP_INCRSAT: return stencilopIncrSat;
	case D3DSTENCILOP_DECRSAT: return stencilopDecrSat;
	case D3DSTENCILOP_INVERT: return stencilopInvert;
	case D3DSTENCILOP_INCR: return stencilopIncr;
	case D3DSTENCILOP_DECR: return stencilopDecr;
	default: return stencilopKeep;
	}
}

static gfxTexAddress sMapAddress(DWORD a)
{
	switch (a) {
	case D3DTADDRESS_CLAMP:
	case D3DTADDRESS_BORDER: return texaddrClamp;
	case D3DTADDRESS_MIRROR:
	case D3DTADDRESS_MIRRORONCE: return texaddrMirror;
	default: return texaddrWrap;
	}
}

static gfxTexFilter sMapFilter(DWORD f)
{
	switch (f) {
	case D3DTEXF_NONE: return texfilterNone;
	case D3DTEXF_POINT: return texfilterPoint;
	default: return texfilterLinear;
	}
}

// One-shot diagnostics for state the backend cannot express.
static void sUnsupported(const char *what, DWORD value)
{
	enum { MAX_REPORTS = 32 };
	static const char *reported[MAX_REPORTS];
	static DWORD reportedValue[MAX_REPORTS];
	static int count = 0;
	for (int i = 0; i < count; i++)
		if (reported[i] == what && reportedValue[i] == value) return;
	if (count < MAX_REPORTS) { reported[count] = what; reportedValue[count] = value; count++; }
	Displayf("[d3dcompat] unsupported %s (0x%x) ignored", what, (unsigned)value);
}

HRESULT gfxD3DCheck(HRESULT hr, const char *expr, const char *file, int line)
{
	if (FAILED(hr)) {
		static int reports = 0;
		if (reports < 16) {
			reports++;
			Displayf("[d3dcompat] %s failed (0x%08x) at %s(%d)", expr, (unsigned)hr, file, line);
		}
	}
	return hr;
}

////////////////////////////////////////////////////////////////////////////////

gfxD3DCompatDevice::gfxD3DCompatDevice()
{
	memset(m_Stage, 0, sizeof(m_Stage));
	memset(m_RenderState, 0, sizeof(m_RenderState));
	m_FVF = 0;
	m_VertexShader = 0;
	m_PixelShader = 0;
	// D3D defaults (recorded only; nothing is pushed to RSTATE until the
	// game changes something, so engine-side defaults stay in charge).
	m_RenderState[D3DRS_ZENABLE] = 1;
	m_RenderState[D3DRS_ZWRITEENABLE] = 1;
	m_RenderState[D3DRS_ZFUNC] = D3DCMP_LESSEQUAL;
	m_RenderState[D3DRS_SRCBLEND] = D3DBLEND_ONE;
	m_RenderState[D3DRS_DESTBLEND] = D3DBLEND_ZERO;
	m_RenderState[D3DRS_CULLMODE] = D3DCULL_CCW;
	m_RenderState[D3DRS_ALPHAFUNC] = D3DCMP_ALWAYS;
	m_RenderState[D3DRS_FILLMODE] = D3DFILL_SOLID;
	m_RenderState[D3DRS_STENCILFUNC] = D3DCMP_ALWAYS;
	m_RenderState[D3DRS_STENCILFAIL] = D3DSTENCILOP_KEEP;
	m_RenderState[D3DRS_STENCILZFAIL] = D3DSTENCILOP_KEEP;
	m_RenderState[D3DRS_STENCILPASS] = D3DSTENCILOP_KEEP;
	m_RenderState[D3DRS_STENCILMASK] = 0xffffffff;
	m_RenderState[D3DRS_STENCILWRITEMASK] = 0xffffffff;
	m_RenderState[D3DRS_TEXTUREFACTOR] = 0xffffffff;
	m_RenderState[D3DRS_COLORWRITEENABLE] = 0xf;
	m_RenderState[D3DRS_LIGHTING] = 1;
	m_RenderState[D3DRS_FOGCOLOR] = 0;
	for (int s = 0; s < NUM_STAGES; s++) {
		Stage &st = m_Stage[s];
		st.ColorOp = (s == 0) ? D3DTOP_MODULATE : D3DTOP_DISABLE;
		st.ColorArg1 = D3DTA_TEXTURE;
		st.ColorArg2 = D3DTA_CURRENT;
		st.AlphaOp = (s == 0) ? D3DTOP_SELECTARG1 : D3DTOP_DISABLE;
		st.AlphaArg1 = D3DTA_TEXTURE;
		st.AlphaArg2 = D3DTA_CURRENT;
		st.AddressU = st.AddressV = st.AddressW = D3DTADDRESS_WRAP;
		st.MagFilter = st.MinFilter = D3DTEXF_LINEAR;
		st.MipFilter = D3DTEXF_LINEAR;
		st.TexCoordIndex = (DWORD)s;
		st.TransformFlags = D3DTTFF_DISABLE;
		st.MipLodBias = 0.0f;
		st.Texture = NULL;
	}
}

void gfxD3DCompatDevice::Reset()
{
	*this = gfxD3DCompatDevice();
	ApplyStencil();
	ApplyAlphaTest();
	ApplyBlend();
	ApplyColorWrite();
	for (int s = 0; s < 2; s++) ApplyStage(s);
}

//// Render states //////////////////////////////////////////////////////////////

HRESULT gfxD3DCompatDevice::SetRenderState(int state, DWORD value)
{
	if (state < 0 || state >= NUM_RENDER_STATES) return D3DERR_INVALIDCALL;
	m_RenderState[state] = value;

	switch (state) {
	case D3DRS_ZENABLE: RSTATE.SetZTestEnable(value != 0); break;
	case D3DRS_ZWRITEENABLE: RSTATE.SetZWriteEnable(value != 0); break;
	case D3DRS_ZFUNC: RSTATE.SetZFunc(sMapCmp(value)); break;
	case D3DRS_ZBIAS: RSTATE.SetZBias(-(float)(int)value); break;   // D3D8: 0..16 pulls toward the viewer

	case D3DRS_ALPHATESTENABLE:
	case D3DRS_ALPHAFUNC:
	case D3DRS_ALPHAREF: ApplyAlphaTest(); break;

	case D3DRS_ALPHABLENDENABLE: RSTATE.SetAlphaBlendEnable(value != 0); break;
	case D3DRS_SRCBLEND:
	case D3DRS_DESTBLEND:
	case D3DRS_BLENDOP: ApplyBlend(); break;

	case D3DRS_CULLMODE:
		RSTATE.SetCull(value == D3DCULL_NONE ? cullNone : (value == D3DCULL_CW ? cullCW : cullCCW));
		break;
	case D3DRS_FILLMODE:
		RSTATE.SetFillMode(value == D3DFILL_WIREFRAME ? fillWire : (value == D3DFILL_POINT ? fillPoint : fillSolid));
		break;
	case D3DRS_LIGHTING: RSTATE.SetLighting(value != 0); break;

	case D3DRS_FOGENABLE: RSTATE.SetFogEnable(value != 0); break;
	case D3DRS_FOGCOLOR:
	case D3DRS_FOGSTART:
	case D3DRS_FOGEND: ApplyFog(); break;

	case D3DRS_STENCILENABLE:
	case D3DRS_STENCILFAIL:
	case D3DRS_STENCILZFAIL:
	case D3DRS_STENCILPASS:
	case D3DRS_STENCILFUNC:
	case D3DRS_STENCILREF:
	case D3DRS_STENCILMASK:
	case D3DRS_STENCILWRITEMASK: ApplyStencil(); break;

	case D3DRS_TEXTUREFACTOR: RSTATE.SetTextureFactor((u32)value); break;
	case D3DRS_COLORWRITEENABLE: ApplyColorWrite(); break;
	case D3DRS_DITHERENABLE: RSTATE.SetDither(value != 0); break;

	// Recorded only: no meaning on the D3D11 backend.
	case D3DRS_SOFTWAREVERTEXPROCESSING:
	case D3DRS_CLIPPING:
	case D3DRS_SHADEMODE:
	case D3DRS_LASTPIXEL:
	case D3DRS_SPECULARENABLE:
	case D3DRS_FOGTABLEMODE:
	case D3DRS_FOGVERTEXMODE:
	case D3DRS_FOGDENSITY:
	case D3DRS_RANGEFOGENABLE:
	case D3DRS_EDGEANTIALIAS:
	case D3DRS_AMBIENT:
	case D3DRS_COLORVERTEX:
	case D3DRS_LOCALVIEWER:
	case D3DRS_NORMALIZENORMALS:
	case D3DRS_DIFFUSEMATERIALSOURCE:
	case D3DRS_SPECULARMATERIALSOURCE:
	case D3DRS_AMBIENTMATERIALSOURCE:
	case D3DRS_EMISSIVEMATERIALSOURCE:
	case D3DRS_VERTEXBLEND:
	case D3DRS_CLIPPLANEENABLE:
	case D3DRS_MULTISAMPLEANTIALIAS:
	case D3DRS_MULTISAMPLEMASK:
		break;

	default:
		sUnsupported("render state", (DWORD)state);
		break;
	}
	return D3D_OK;
}

HRESULT gfxD3DCompatDevice::GetRenderState(int state, DWORD *value) const
{
	if (state < 0 || state >= NUM_RENDER_STATES || !value) return D3DERR_INVALIDCALL;
	*value = m_RenderState[state];
	return D3D_OK;
}

void gfxD3DCompatDevice::ApplyAlphaTest()
{
	bool on = m_RenderState[D3DRS_ALPHATESTENABLE] != 0;
	RSTATE.SetAlphaFunc(on ? sMapAlphaCmp(m_RenderState[D3DRS_ALPHAFUNC]) : (int)alphaAlways);
	RSTATE.SetAlphaRef((int)(m_RenderState[D3DRS_ALPHAREF] & 0xff));
}

void gfxD3DCompatDevice::ApplyBlend()
{
	RSTATE.SetBlendSet(gfxD3DBlendSetFromPair(m_RenderState[D3DRS_SRCBLEND], m_RenderState[D3DRS_DESTBLEND], m_RenderState[D3DRS_BLENDOP]));
}

EnumBlendSet gfxD3DBlendSetFromPair(DWORD src, DWORD dst, DWORD op)
{
	EnumBlendSet set = blendSet_One_Zero;
	bool known = true;

	if (op == D3DBLENDOP_REVSUBTRACT && src == D3DBLEND_ONE && dst == D3DBLEND_ONE) set = blendSet_MinusOne_One;
	else if (src == D3DBLEND_ONE && dst == D3DBLEND_ZERO) set = blendSet_One_Zero;
	else if (src == D3DBLEND_SRCALPHA && dst == D3DBLEND_INVSRCALPHA) set = blendSet_SrcAlpha_InvSrcAlpha;
	else if (src == D3DBLEND_ONE && dst == D3DBLEND_ONE) set = blendSet_One_One;
	else if (src == D3DBLEND_ONE && dst == D3DBLEND_SRCALPHA) set = blendSet_One_SrcAlpha;
	else if (src == D3DBLEND_SRCALPHA && dst == D3DBLEND_ONE) set = blendSet_SrcAlpha_One;
	else if (src == D3DBLEND_INVSRCALPHA && dst == D3DBLEND_SRCALPHA) set = blendSet_InvSrcAlpha_SrcAlpha;
	else if ((src == D3DBLEND_DESTCOLOR && dst == D3DBLEND_ZERO) || (src == D3DBLEND_ZERO && dst == D3DBLEND_SRCCOLOR)) set = blendSet_DestColor_Zero;
	else if (src == D3DBLEND_INVSRCALPHA && dst == D3DBLEND_ZERO) set = blendSet_InvSrcAlpha_Zero;
	else if (src == D3DBLEND_SRCALPHA && dst == D3DBLEND_ZERO) set = blendSet_SrcAlpha_Zero;
	else if (src == D3DBLEND_DESTALPHA && dst == D3DBLEND_ONE) set = blendSet_DestAlpha_One;
	else if (src == D3DBLEND_DESTALPHA && dst == D3DBLEND_INVDESTALPHA) set = blendSet_DestAlpha_InvDestAlpha;
	else known = false;

	if (!known) sUnsupported("blend pair", (src << 8) | dst);
	return set;
}

void gfxD3DCompatDevice::ApplyStencil()
{
	gfxStencilState s;
	s.enable = m_RenderState[D3DRS_STENCILENABLE] != 0;
	s.func = sMapCmp(m_RenderState[D3DRS_STENCILFUNC]);
	s.ref = (int)m_RenderState[D3DRS_STENCILREF];
	s.readMask = m_RenderState[D3DRS_STENCILMASK];
	s.writeMask = m_RenderState[D3DRS_STENCILWRITEMASK];
	s.pass = sMapStencilOp(m_RenderState[D3DRS_STENCILPASS]);
	s.fail = sMapStencilOp(m_RenderState[D3DRS_STENCILFAIL]);
	s.zfail = sMapStencilOp(m_RenderState[D3DRS_STENCILZFAIL]);
	RSTATE.SetStencil(s);
}

void gfxD3DCompatDevice::ApplyFog()
{
	union { DWORD u; float f; } start, end;
	start.u = m_RenderState[D3DRS_FOGSTART];
	end.u = m_RenderState[D3DRS_FOGEND];
	RSTATE.SetFogParams(m_RenderState[D3DRS_FOGCOLOR], start.f, end.f, 0.0f, 1.0f);
}

void gfxD3DCompatDevice::ApplyColorWrite()
{
	DWORD m = m_RenderState[D3DRS_COLORWRITEENABLE];
	RSTATE.SetColorMask((m & D3DCOLORWRITEENABLE_RED) != 0, (m & D3DCOLORWRITEENABLE_GREEN) != 0,
	                    (m & D3DCOLORWRITEENABLE_BLUE) != 0, (m & D3DCOLORWRITEENABLE_ALPHA) != 0);
}

//// Texture stages /////////////////////////////////////////////////////////////

HRESULT gfxD3DCompatDevice::SetTextureStageState(DWORD stage, int type, DWORD value)
{
	if (stage >= (DWORD)NUM_STAGES) return D3DERR_INVALIDCALL;
	Stage &st = m_Stage[stage];
	union { DWORD u; float f; } bits;
	bits.u = value;

	switch (type) {
	case D3DTSS_COLOROP: st.ColorOp = value; break;
	case D3DTSS_COLORARG1: st.ColorArg1 = value; break;
	case D3DTSS_COLORARG2: st.ColorArg2 = value; break;
	case D3DTSS_ALPHAOP: st.AlphaOp = value; break;
	case D3DTSS_ALPHAARG1: st.AlphaArg1 = value; break;
	case D3DTSS_ALPHAARG2: st.AlphaArg2 = value; break;
	case D3DTSS_ADDRESSU: case D3DSAMP_ADDRESSU: st.AddressU = value; break;
	case D3DTSS_ADDRESSV: case D3DSAMP_ADDRESSV: st.AddressV = value; break;
	case D3DTSS_ADDRESSW: case D3DSAMP_ADDRESSW: st.AddressW = value; break;
	case D3DTSS_MAGFILTER: case D3DSAMP_MAGFILTER: st.MagFilter = value; break;
	case D3DTSS_MINFILTER: case D3DSAMP_MINFILTER: st.MinFilter = value; break;
	case D3DTSS_MIPFILTER: case D3DSAMP_MIPFILTER: st.MipFilter = value; break;
	case D3DTSS_MIPMAPLODBIAS: case D3DSAMP_MIPMAPLODBIAS: st.MipLodBias = bits.f; break;
	case D3DTSS_TEXCOORDINDEX: st.TexCoordIndex = value; break;
	case D3DTSS_TEXTURETRANSFORMFLAGS: st.TransformFlags = value; break;
	case D3DTSS_BORDERCOLOR: case D3DSAMP_BORDERCOLOR:
	case D3DTSS_MAXMIPLEVEL: case D3DSAMP_MAXMIPLEVEL:
	case D3DTSS_MAXANISOTROPY: case D3DSAMP_MAXANISOTROPY:
	case D3DTSS_RESULTARG: case D3DTSS_COLORARG0: case D3DTSS_ALPHAARG0:
		break;   // recorded nowhere: no backend meaning
	case D3DTSS_BUMPENVMAT00: case D3DTSS_BUMPENVMAT01: case D3DTSS_BUMPENVMAT10: case D3DTSS_BUMPENVMAT11:
	case D3DTSS_BUMPENVLSCALE: case D3DTSS_BUMPENVLOFFSET:
		sUnsupported("bump-env stage state", (DWORD)type);
		break;
	default:
		sUnsupported("texture stage state", (DWORD)type);
		return D3DERR_INVALIDCALL;
	}
	ApplyStage((int)stage);
	return D3D_OK;
}

HRESULT gfxD3DCompatDevice::GetTextureStageState(DWORD stage, int type, DWORD *value) const
{
	if (stage >= (DWORD)NUM_STAGES || !value) return D3DERR_INVALIDCALL;
	const Stage &st = m_Stage[stage];
	union { DWORD u; float f; } bits;
	switch (type) {
	case D3DTSS_COLOROP: *value = st.ColorOp; break;
	case D3DTSS_COLORARG1: *value = st.ColorArg1; break;
	case D3DTSS_COLORARG2: *value = st.ColorArg2; break;
	case D3DTSS_ALPHAOP: *value = st.AlphaOp; break;
	case D3DTSS_ALPHAARG1: *value = st.AlphaArg1; break;
	case D3DTSS_ALPHAARG2: *value = st.AlphaArg2; break;
	case D3DTSS_ADDRESSU: case D3DSAMP_ADDRESSU: *value = st.AddressU; break;
	case D3DTSS_ADDRESSV: case D3DSAMP_ADDRESSV: *value = st.AddressV; break;
	case D3DTSS_ADDRESSW: case D3DSAMP_ADDRESSW: *value = st.AddressW; break;
	case D3DTSS_MAGFILTER: case D3DSAMP_MAGFILTER: *value = st.MagFilter; break;
	case D3DTSS_MINFILTER: case D3DSAMP_MINFILTER: *value = st.MinFilter; break;
	case D3DTSS_MIPFILTER: case D3DSAMP_MIPFILTER: *value = st.MipFilter; break;
	case D3DTSS_MIPMAPLODBIAS: case D3DSAMP_MIPMAPLODBIAS: bits.f = st.MipLodBias; *value = bits.u; break;
	case D3DTSS_TEXCOORDINDEX: *value = st.TexCoordIndex; break;
	case D3DTSS_TEXTURETRANSFORMFLAGS: *value = st.TransformFlags; break;
	default: *value = 0; return D3DERR_INVALIDCALL;
	}
	return D3D_OK;
}

// Which colour source a SELECTARG op picks, as the engine's texture op.
static gfxTextureOp sSelectOp(DWORD arg)
{
	switch (arg & D3DTA_SELECTMASK) {
	case D3DTA_TEXTURE: return texopSelectArg1;
	case D3DTA_TFACTOR: return texopSelectArg2;   // constant colour: closest fixed-function equivalent is the vertex colour path
	default: return texopSelectArg2;              // DIFFUSE / CURRENT
	}
}

// D3D8 stage-1 ops -> the backend's stage-2 combine.  An arg of TEXTURE
// selects the stage-1 texture, anything else (CURRENT / DIFFUSE / TFACTOR)
// the running result.
static int sStage1ColorOp(DWORD op, DWORD arg1, DWORD arg2)
{
	switch (op) {
	case D3DTOP_SELECTARG1: return ((arg1 & D3DTA_SELECTMASK) == D3DTA_TEXTURE) ? tex2colTexture : tex2colCurrent;
	case D3DTOP_SELECTARG2: return ((arg2 & D3DTA_SELECTMASK) == D3DTA_TEXTURE) ? tex2colTexture : tex2colCurrent;
	case D3DTOP_BLENDTEXTUREALPHA: return tex2colDecal;
	default: return tex2colModulate;   // MODULATE / MODULATE2X / MODULATE4X
	}
}

static int sStage1AlphaOp(DWORD op, DWORD arg1, DWORD arg2)
{
	switch (op) {
	case D3DTOP_DISABLE: return tex2alphaCurrent;
	case D3DTOP_SELECTARG1: return ((arg1 & D3DTA_SELECTMASK) == D3DTA_TEXTURE) ? tex2alphaTexture : tex2alphaCurrent;
	case D3DTOP_SELECTARG2: return ((arg2 & D3DTA_SELECTMASK) == D3DTA_TEXTURE) ? tex2alphaTexture : tex2alphaCurrent;
	default: return tex2alphaModulate;
	}
}

void gfxD3DCompatDevice::ApplyStage(int stage)
{
	if (stage < 0 || stage > 1) return;   // stages 2/3 have no backend
	const Stage &st = m_Stage[stage];

	// Sampler
	RSTATE.SetTextureAddress(stage, sMapAddress(st.AddressU), sMapAddress(st.AddressV));
	gfxTexFilter minMag = (st.MinFilter == D3DTEXF_POINT && st.MagFilter == D3DTEXF_POINT) ? texfilterPoint : texfilterLinear;
	RSTATE.SetTextureFilter(stage, minMag, sMapFilter(st.MipFilter));
	RSTATE.SetMipLodBias(stage, st.MipLodBias);

	// Texture coordinate generation / transform
	DWORD tci = st.TexCoordIndex;
	if (tci & 0xffff0000) {
		sUnsupported("camera-space texgen", tci & 0xffff0000);
		RSTATE.SetTexGeneration(stage, 0, false, (int)(tci & 0xffff), 0);
	} else if (st.TransformFlags != D3DTTFF_DISABLE) {
		RSTATE.SetTexGeneration(stage, 2, false, (int)tci, 0);
	} else {
		RSTATE.SetTexGeneration(stage, 0, false, (int)tci, 0);
	}

	// Colour op
	gfxTextureOp colorOp;
	switch (st.ColorOp) {
	case D3DTOP_DISABLE: colorOp = texopDisable; break;
	case D3DTOP_SELECTARG1: colorOp = sSelectOp(st.ColorArg1); break;
	case D3DTOP_SELECTARG2: colorOp = sSelectOp(st.ColorArg2); break;
	case D3DTOP_MODULATE: colorOp = texopModulate; break;
	case D3DTOP_MODULATE2X:
	case D3DTOP_MODULATE4X: colorOp = texopModulate2X; break;
	default:
		sUnsupported("texture colour op", st.ColorOp);
		colorOp = texopModulate;
		break;
	}

	if (stage == 0) {
		RSTATE.SetTexEnable(colorOp != texopDisable);
		if (colorOp != texopDisable) RSTATE.SetTexColorOp(0, colorOp);
		gfxTextureOp alphaOp = texopModulate;
		switch (st.AlphaOp) {
		case D3DTOP_DISABLE: alphaOp = texopDisable; break;
		case D3DTOP_SELECTARG1: alphaOp = sSelectOp(st.AlphaArg1); break;
		case D3DTOP_SELECTARG2: alphaOp = sSelectOp(st.AlphaArg2); break;
		default: alphaOp = texopModulate; break;
		}
		RSTATE.SetTexAlphaOp(0, alphaOp);
	} else {
		// Stage 1 is the backend's second sampler.  The colour op decides what
		// the RGB becomes (MODULATE = current * texture, SELECTARG picks the
		// current result or the texture), the alpha op likewise for alpha
		// (the game's sun-glow mask: colour = CURRENT, alpha = TEXTURE).
		if (colorOp == texopDisable) {
			vglBindTexture2(NULL);
		} else {
			vglBindTexture2(st.Texture);
			vglTex2CombineOps(sStage1ColorOp(st.ColorOp, st.ColorArg1, st.ColorArg2),
			                  sStage1AlphaOp(st.AlphaOp, st.AlphaArg1, st.AlphaArg2));
		}
	}
}

HRESULT gfxD3DCompatDevice::SetTexture(DWORD stage, IDirect3DBaseTexture8 *tex)
{
	if (stage >= (DWORD)NUM_STAGES) return D3DERR_INVALIDCALL;
	m_Stage[stage].Texture = tex;
	if (stage == 0) {
		RSTATE.SetTexture(tex);
	} else if (stage == 1) {
		// Binding resets the combine; re-apply the stage's current ops.
		if (m_Stage[1].ColorOp != D3DTOP_DISABLE) ApplyStage(1);
	}
	return D3D_OK;
}

HRESULT gfxD3DCompatDevice::GetTexture(DWORD stage, IDirect3DBaseTexture8 **tex) const
{
	if (stage >= (DWORD)NUM_STAGES || !tex) return D3DERR_INVALIDCALL;
	*tex = m_Stage[stage].Texture;
	if (*tex) (*tex)->AddRef();
	return D3D_OK;
}

//// Vertex layout / draws //////////////////////////////////////////////////////

HRESULT gfxD3DCompatDevice::SetVertexShader(DWORD handle)
{
	m_VertexShader = handle;
	// D3D8: a handle without the RESERVED0 bit that carries a position type
	// is an FVF code, and that is how the fixed-function pipeline is selected.
	if (handle && !(handle & D3DFVF_RESERVED0) && (handle & D3DFVF_POSITION_MASK))
		m_FVF = handle;
	return D3D_OK;
}

int gfxD3DCompatDevice::PrimitiveVertexCount(int primType, UINT primCount)
{
	switch (primType) {
	case D3DPT_POINTLIST: return (int)primCount;
	case D3DPT_LINELIST: return (int)primCount * 2;
	case D3DPT_LINESTRIP: return (int)primCount + 1;
	case D3DPT_TRIANGLELIST: return (int)primCount * 3;
	case D3DPT_TRIANGLESTRIP:
	case D3DPT_TRIANGLEFAN: return (int)primCount + 2;
	default: return 0;
	}
}

static int sTexCoordSize(DWORD fvf, int index)
{
	switch ((fvf >> (16 + index * 2)) & 3) {
	case 3: return 1;
	case 1: return 3;
	case 2: return 4;
	default: return 2;
	}
}

UINT gfxD3DCompatDevice::FVFVertexSize(DWORD fvf)
{
	UINT size = 0;
	switch (fvf & D3DFVF_POSITION_MASK) {
	case D3DFVF_XYZ: size += 12; break;
	case D3DFVF_XYZRHW: size += 16; break;
	case D3DFVF_XYZB1: size += 16; break;
	case D3DFVF_XYZB2: size += 20; break;
	case D3DFVF_XYZB3: size += 24; break;
	case D3DFVF_XYZB4: size += 28; break;
	case D3DFVF_XYZB5: size += 32; break;
	default: break;
	}
	if (fvf & D3DFVF_NORMAL) size += 12;
	if (fvf & D3DFVF_PSIZE) size += 4;
	if (fvf & D3DFVF_DIFFUSE) size += 4;
	if (fvf & D3DFVF_SPECULAR) size += 4;
	int texCount = (int)((fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT);
	for (int i = 0; i < texCount; i++) size += (UINT)sTexCoordSize(fvf, i) * 4;
	return size;
}

HRESULT gfxD3DCompatDevice::DrawPrimitiveUP(int primType, UINT primCount, const void *verts, UINT stride)
{
	return DrawVerticesUP(primType, (UINT)PrimitiveVertexCount(primType, primCount), verts, stride);
}

HRESULT gfxD3DCompatDevice::DrawVerticesUP(int primType, UINT vertexCount, const void *verts, UINT stride)
{
	if (!verts || vertexCount == 0) return D3DERR_INVALIDCALL;
	DWORD fvf = m_FVF;
	if (!(fvf & D3DFVF_POSITION_MASK)) {
		sUnsupported("DrawPrimitiveUP without an FVF", fvf);
		return D3DERR_INVALIDCALL;
	}
	if (stride == 0) stride = FVFVertexSize(fvf);

	EnumDrawType prim;
	switch (primType) {
	case D3DPT_POINTLIST: prim = drawPoints; break;
	case D3DPT_LINELIST: prim = drawLine; break;
	case D3DPT_LINESTRIP: prim = drawLineStrip; break;
	case D3DPT_TRIANGLELIST: prim = drawTri; break;
	case D3DPT_TRIANGLESTRIP: prim = drawTriStrip; break;
	case D3DPT_TRIANGLEFAN: prim = drawTriFan; break;
	default: return D3DERR_INVALIDCALL;
	}
	if ((int)vertexCount > vglBeginMax) {
		sUnsupported("DrawPrimitiveUP batch larger than vglBeginMax", vertexCount);
		vertexCount = (UINT)vglBeginMax;
	}

	// Pre-transformed vertices are screen pixels: draw through the ortho projection.
	bool rhw = (fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW;
	bool wasOrtho = PIPE.GetViewport() && PIPE.GetViewport() == PIPE.GetOrthoViewport();
	if (rhw && !wasOrtho) vglSetViewportOrtho(true);

	int texCount = (int)((fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT);
	const unsigned char *p = (const unsigned char *)verts;

	vglBegin(prim, (int)vertexCount);
	for (UINT i = 0; i < vertexCount; i++, p += stride) {
		const unsigned char *q = p;
		const float *pos = (const float *)q;
		switch (fvf & D3DFVF_POSITION_MASK) {
		case D3DFVF_XYZ: q += 12; break;
		case D3DFVF_XYZRHW: q += 16; break;
		case D3DFVF_XYZB1: q += 16; break;
		case D3DFVF_XYZB2: q += 20; break;
		case D3DFVF_XYZB3: q += 24; break;
		case D3DFVF_XYZB4: q += 28; break;
		case D3DFVF_XYZB5: q += 32; break;
		default: break;
		}
		if (fvf & D3DFVF_NORMAL) {
			const float *n = (const float *)q;
			vglNormal3f(n[0], n[1], n[2]);
			q += 12;
		}
		if (fvf & D3DFVF_PSIZE) q += 4;
		if (fvf & D3DFVF_DIFFUSE) {
			vglColor((gfxPackedColor)*(const u32 *)q);   // D3DCOLOR is ARGB, same packing as gfxPackedColor
			q += 4;
		}
		if (fvf & D3DFVF_SPECULAR) q += 4;
		for (int t = 0; t < texCount; t++) {
			const float *uv = (const float *)q;
			int n = sTexCoordSize(fvf, t);
			if (t == 0) vglTexCoord2f(uv[0], n > 1 ? uv[1] : 0.0f);
			else if (t == 1) vglTexCoord2f2(uv[0], n > 1 ? uv[1] : 0.0f);
			q += n * 4;
		}
		vglVertex3f(pos[0], pos[1], pos[2]);
	}
	vglEnd();

	if (rhw && !wasOrtho) vglSetViewportOrtho(false);
	return D3D_OK;
}

//// Surfaces ///////////////////////////////////////////////////////////////////

// Surfaces view a texture: same size and GPU resources, own reference.
IDirect3DSurface8::IDirect3DSurface8(gfxTexture *source) : Source(source)
{
	if (Source) {
		Source->AddRef();
		gfxAliasTexture(this, Source);
	}
}

static IDirect3DSurface8 *sWrapSurface(gfxTexture *tex)
{
	if (!tex) return NULL;
	IDirect3DSurface8 *s = new IDirect3DSurface8(tex);
	tex->Release();   // the wrapper holds its own reference now
	return s;
}

static gfxTexture *sUnwrap(gfxTexture *texOrSurface)
{
	IDirect3DSurface8 *s = dynamic_cast<IDirect3DSurface8 *>(texOrSurface);
	return (s && s->Source) ? s->Source : texOrSurface;
}

HRESULT gfxD3DCompatDevice::GetBackBuffer(int index, int /*type*/, IDirect3DSurface8 **out)
{
	if (!out) return D3DERR_INVALIDCALL;
	*out = NULL;
	if (index < 0) gfxRefreshBackBufferCopy();   // front buffer: the finished frame
	gfxTexture *bb = gfxTexture::CreateRenderTarget(PIPE.GetWidth(), PIPE.GetHeight(), gfxTexture::rtargetBackFBMem);
	if (!bb) return D3DERR_INVALIDCALL;
	*out = sWrapSurface(bb);
	return D3D_OK;
}

HRESULT gfxD3DCompatDevice::GetRenderTarget(IDirect3DSurface8 **out)
{
	return GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, out);
}

HRESULT gfxD3DCompatDevice::GetDepthStencilSurface(IDirect3DSurface8 **out)
{
	if (!out) return D3DERR_INVALIDCALL;
	*out = sWrapSurface(gfxTexture::CreateRenderTarget(PIPE.GetWidth(), PIPE.GetHeight(), gfxTexture::rtargetZBuff));
	return *out ? D3D_OK : D3DERR_INVALIDCALL;
}

HRESULT gfxD3DCompatDevice::CreateImageSurface(UINT width, UINT height, int /*format*/, IDirect3DSurface8 **out)
{
	if (!out) return D3DERR_INVALIDCALL;
	*out = sWrapSurface(gfxTexture::CreateRenderTarget((int)width, (int)height, gfxTexture::rtargetTexMem | gfxTexture::rtarget32Bits));
	return *out ? D3D_OK : D3DERR_INVALIDCALL;
}

HRESULT gfxD3DCompatDevice::CopyRects(IDirect3DSurface8 *src, const void *srcRects, UINT numRects, IDirect3DSurface8 *dst, const void * /*dstPoints*/)
{
	if (!src || !dst) return D3DERR_INVALIDCALL;
	if (srcRects && numRects) sUnsupported("CopyRects with sub-rectangles (whole surface copied)", numRects);
	return gfxCopyTexture(sUnwrap(dst), sUnwrap(src)) ? D3D_OK : D3DERR_INVALIDCALL;
}

HRESULT gfxD3DCompatDevice::Clear(DWORD count, const D3DRECT *rects, DWORD flags, D3DCOLOR color, float /*z*/, DWORD /*stencil*/)
{
	u32 pipeFlags = 0;
	if (flags & D3DCLEAR_TARGET) pipeFlags |= gfxPipeline::clearColor;
	if (flags & D3DCLEAR_ZBUFFER) pipeFlags |= gfxPipeline::clearZ;
	if (flags & D3DCLEAR_STENCIL) pipeFlags |= gfxPipeline::clearStencil;

	bool alphaOnly = (flags & D3DCLEAR_TARGET_A) != 0 && !(flags & D3DCLEAR_TARGET);
	if (alphaOnly) RSTATE.SetColorMask(false, true);

	if (count && rects) {
		for (DWORD i = 0; i < count; i++) {
			const D3DRECT &r = rects[i];
			if (flags & (D3DCLEAR_TARGET | D3DCLEAR_TARGET_A))
				PIPE.ClearRect((int)r.x1, (int)r.y1, (int)(r.x2 - r.x1), (int)(r.y2 - r.y1), (u32)color);
		}
		if (pipeFlags & (gfxPipeline::clearZ | gfxPipeline::clearStencil))
			sUnsupported("rect-limited depth/stencil clear (whole target cleared)", pipeFlags);
		if (pipeFlags & ~(u32)gfxPipeline::clearColor) PIPE.Clear(pipeFlags & ~(u32)gfxPipeline::clearColor, (u32)color);
	} else {
		if (alphaOnly) pipeFlags |= gfxPipeline::clearColor;
		PIPE.Clear(pipeFlags, (u32)color);
	}

	if (alphaOnly) ApplyColorWrite();
	return D3D_OK;
}
