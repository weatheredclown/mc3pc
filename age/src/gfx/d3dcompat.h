#ifndef GFX_D3DCOMPAT_H
#define GFX_D3DCOMPAT_H

////////////////////////////////////////
// gfx/d3dcompat.h
//
// Fixed-function Direct3D 8 compatibility layer.
//
// MC3's PC/Xbox paths drive the renderer through a D3D8-style device
// (lpD3DDev->SetRenderState / SetTextureStageState / SetTexture /
// DrawPrimitiveUP / GetBackBuffer ...).  This header gives those calls a
// home without any Direct3D or Windows header: the D3D8 state vocabulary is
// declared here as plain enums (D3D8 numbering, so values in the game read
// the same), surfaces and textures are gfxTexture, and gfxD3DCompatDevice
// translates each state change onto the engine render state (RSTATE), the
// immediate-mode vertex path (vgl*) and the pipeline (PIPE).
//
// What maps where:
//   render states   -> RSTATE z/alpha-test/blend/cull/fog/stencil/colour mask
//   stage 0 ops     -> RSTATE.SetTexColorOp / SetTexEnable / SetTexAlphaOp
//   stage 1 ops     -> vglBindTexture2 / vglTex2CombineOps (MODULATE, SELECTARG
//                      current/texture and BLENDTEXTUREALPHA, colour and alpha separately)
//   samplers        -> RSTATE.SetTextureAddress / SetTextureFilter / SetMipLodBias
//   TEXTUREFACTOR   -> RSTATE.SetTextureFactor
//   DrawPrimitiveUP -> FVF decode into vglBegin/vglVertex/vglEnd
//   back buffers    -> the pipeline's backbuffer copy (gfxGetBackBufferCopy)
// Unsupported combinations (texgen from camera-space normals, ADD/LERP
// stage ops, bump env) are recorded and reported once, never faked.
//
// Vertex/pixel "shaders": the D3D11 backend derives its vertex layout from
// the vgl streams, so SetVertexShader(FVF) only records the FVF and shader
// handles are ignored; gfxPixelShaders stays 0 so the game takes its
// fixed-function branches.
////////////////////////////////////////

#include "core/types.h"
#include "gfx/statetypes.h"
#include "gfx/texture.h"

// Windows-style scalar names the game spells (identical to the SDK typedefs,
// so including <windows.h> alongside is harmless).
typedef unsigned long DWORD;
typedef long HRESULT;
typedef unsigned int UINT;

#ifndef D3D_OK
#define D3D_OK 0
#endif
#ifndef D3DERR_INVALIDCALL
#define D3DERR_INVALIDCALL ((HRESULT)0x8876086CL)
#endif
#ifndef SUCCEEDED
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define FAILED(hr) (((HRESULT)(hr)) < 0)
#endif

// --- D3D8 state vocabulary --------------------------------------------------

enum D3DRENDERSTATETYPE {
	D3DRS_ZENABLE = 7, D3DRS_FILLMODE = 8, D3DRS_SHADEMODE = 9,
	D3DRS_ZWRITEENABLE = 14, D3DRS_ALPHATESTENABLE = 15, D3DRS_LASTPIXEL = 16,
	D3DRS_SRCBLEND = 19, D3DRS_DESTBLEND = 20, D3DRS_CULLMODE = 22, D3DRS_ZFUNC = 23,
	D3DRS_ALPHAREF = 24, D3DRS_ALPHAFUNC = 25, D3DRS_DITHERENABLE = 26,
	D3DRS_ALPHABLENDENABLE = 27, D3DRS_FOGENABLE = 28, D3DRS_SPECULARENABLE = 29,
	D3DRS_FOGCOLOR = 34, D3DRS_FOGTABLEMODE = 35, D3DRS_FOGSTART = 36, D3DRS_FOGEND = 37,
	D3DRS_FOGDENSITY = 38, D3DRS_EDGEANTIALIAS = 40, D3DRS_ZBIAS = 47, D3DRS_RANGEFOGENABLE = 48,
	D3DRS_STENCILENABLE = 52, D3DRS_STENCILFAIL = 53, D3DRS_STENCILZFAIL = 54, D3DRS_STENCILPASS = 55,
	D3DRS_STENCILFUNC = 56, D3DRS_STENCILREF = 57, D3DRS_STENCILMASK = 58, D3DRS_STENCILWRITEMASK = 59,
	D3DRS_TEXTUREFACTOR = 60,
	D3DRS_WRAP0 = 128, D3DRS_WRAP1 = 129, D3DRS_WRAP2 = 130, D3DRS_WRAP3 = 131,
	D3DRS_CLIPPING = 136, D3DRS_LIGHTING = 137, D3DRS_AMBIENT = 139, D3DRS_FOGVERTEXMODE = 140,
	D3DRS_COLORVERTEX = 141, D3DRS_LOCALVIEWER = 142, D3DRS_NORMALIZENORMALS = 143,
	D3DRS_DIFFUSEMATERIALSOURCE = 145, D3DRS_SPECULARMATERIALSOURCE = 146,
	D3DRS_AMBIENTMATERIALSOURCE = 147, D3DRS_EMISSIVEMATERIALSOURCE = 148,
	D3DRS_VERTEXBLEND = 151, D3DRS_CLIPPLANEENABLE = 152, D3DRS_SOFTWAREVERTEXPROCESSING = 153,
	D3DRS_POINTSIZE = 154, D3DRS_POINTSIZE_MIN = 155, D3DRS_POINTSPRITEENABLE = 156,
	D3DRS_POINTSCALEENABLE = 157, D3DRS_MULTISAMPLEANTIALIAS = 161, D3DRS_MULTISAMPLEMASK = 162,
	D3DRS_PATCHEDGESTYLE = 163, D3DRS_POINTSIZE_MAX = 166, D3DRS_INDEXEDVERTEXBLENDENABLE = 167,
	D3DRS_COLORWRITEENABLE = 168, D3DRS_TWEENFACTOR = 170, D3DRS_BLENDOP = 171,
	D3DRS_FORCE_DWORD = 0x7fffffff
};

enum D3DTEXTURESTAGESTATETYPE {
	D3DTSS_COLOROP = 1, D3DTSS_COLORARG1 = 2, D3DTSS_COLORARG2 = 3,
	D3DTSS_ALPHAOP = 4, D3DTSS_ALPHAARG1 = 5, D3DTSS_ALPHAARG2 = 6,
	D3DTSS_BUMPENVMAT00 = 7, D3DTSS_BUMPENVMAT01 = 8, D3DTSS_BUMPENVMAT10 = 9, D3DTSS_BUMPENVMAT11 = 10,
	D3DTSS_TEXCOORDINDEX = 11, D3DTSS_ADDRESSU = 13, D3DTSS_ADDRESSV = 14, D3DTSS_BORDERCOLOR = 15,
	D3DTSS_MAGFILTER = 16, D3DTSS_MINFILTER = 17, D3DTSS_MIPFILTER = 18, D3DTSS_MIPMAPLODBIAS = 19,
	D3DTSS_MAXMIPLEVEL = 20, D3DTSS_MAXANISOTROPY = 21, D3DTSS_BUMPENVLSCALE = 22, D3DTSS_BUMPENVLOFFSET = 23,
	D3DTSS_TEXTURETRANSFORMFLAGS = 24, D3DTSS_ADDRESSW = 25, D3DTSS_COLORARG0 = 26, D3DTSS_ALPHAARG0 = 27,
	D3DTSS_RESULTARG = 28
};

// Sampler states (Xbox / D3D9 spelling of the address + filter stage states).
// Own numbering so a stage-state call and a sampler-state call never collide.
enum D3DSAMPLERSTATETYPE {
	D3DSAMP_ADDRESSU = 0x101, D3DSAMP_ADDRESSV = 0x102, D3DSAMP_ADDRESSW = 0x103,
	D3DSAMP_BORDERCOLOR = 0x104, D3DSAMP_MAGFILTER = 0x105, D3DSAMP_MINFILTER = 0x106,
	D3DSAMP_MIPFILTER = 0x107, D3DSAMP_MIPMAPLODBIAS = 0x108, D3DSAMP_MAXMIPLEVEL = 0x109,
	D3DSAMP_MAXANISOTROPY = 0x10a
};

enum D3DTEXTUREOP {
	D3DTOP_DISABLE = 1, D3DTOP_SELECTARG1 = 2, D3DTOP_SELECTARG2 = 3,
	D3DTOP_MODULATE = 4, D3DTOP_MODULATE2X = 5, D3DTOP_MODULATE4X = 6,
	D3DTOP_ADD = 7, D3DTOP_ADDSIGNED = 8, D3DTOP_ADDSIGNED2X = 9, D3DTOP_SUBTRACT = 10, D3DTOP_ADDSMOOTH = 11,
	D3DTOP_BLENDDIFFUSEALPHA = 12, D3DTOP_BLENDTEXTUREALPHA = 13, D3DTOP_BLENDFACTORALPHA = 14,
	D3DTOP_BLENDTEXTUREALPHAPM = 15, D3DTOP_BLENDCURRENTALPHA = 16, D3DTOP_PREMODULATE = 17,
	D3DTOP_MODULATEALPHA_ADDCOLOR = 18, D3DTOP_MODULATECOLOR_ADDALPHA = 19,
	D3DTOP_MODULATEINVALPHA_ADDCOLOR = 20, D3DTOP_MODULATEINVCOLOR_ADDALPHA = 21,
	D3DTOP_BUMPENVMAP = 22, D3DTOP_BUMPENVMAPLUMINANCE = 23, D3DTOP_DOTPRODUCT3 = 24,
	D3DTOP_MULTIPLYADD = 25, D3DTOP_LERP = 26
};

// Texture argument sources (D3DTSS_COLORARGn / ALPHAARGn)
#define D3DTA_SELECTMASK     0x0000000f
#define D3DTA_DIFFUSE        0x00000000
#define D3DTA_CURRENT        0x00000001
#define D3DTA_TEXTURE        0x00000002
#define D3DTA_TFACTOR        0x00000003
#define D3DTA_SPECULAR       0x00000004
#define D3DTA_TEMP           0x00000005
#define D3DTA_COMPLEMENT     0x00000010
#define D3DTA_ALPHAREPLICATE 0x00000020

enum D3DBLEND {
	D3DBLEND_ZERO = 1, D3DBLEND_ONE = 2, D3DBLEND_SRCCOLOR = 3, D3DBLEND_INVSRCCOLOR = 4,
	D3DBLEND_SRCALPHA = 5, D3DBLEND_INVSRCALPHA = 6, D3DBLEND_DESTALPHA = 7, D3DBLEND_INVDESTALPHA = 8,
	D3DBLEND_DESTCOLOR = 9, D3DBLEND_INVDESTCOLOR = 10, D3DBLEND_SRCALPHASAT = 11,
	D3DBLEND_BOTHSRCALPHA = 12, D3DBLEND_BOTHINVSRCALPHA = 13
};

enum D3DBLENDOP { D3DBLENDOP_ADD = 1, D3DBLENDOP_SUBTRACT = 2, D3DBLENDOP_REVSUBTRACT = 3, D3DBLENDOP_MIN = 4, D3DBLENDOP_MAX = 5 };

enum D3DCMPFUNC {
	D3DCMP_NEVER = 1, D3DCMP_LESS = 2, D3DCMP_EQUAL = 3, D3DCMP_LESSEQUAL = 4,
	D3DCMP_GREATER = 5, D3DCMP_NOTEQUAL = 6, D3DCMP_GREATEREQUAL = 7, D3DCMP_ALWAYS = 8
};

enum D3DSTENCILOP {
	D3DSTENCILOP_KEEP = 1, D3DSTENCILOP_ZERO = 2, D3DSTENCILOP_REPLACE = 3, D3DSTENCILOP_INCRSAT = 4,
	D3DSTENCILOP_DECRSAT = 5, D3DSTENCILOP_INVERT = 6, D3DSTENCILOP_INCR = 7, D3DSTENCILOP_DECR = 8
};

enum D3DTEXTUREADDRESS { D3DTADDRESS_WRAP = 1, D3DTADDRESS_MIRROR = 2, D3DTADDRESS_CLAMP = 3, D3DTADDRESS_BORDER = 4, D3DTADDRESS_MIRRORONCE = 5 };
enum D3DTEXTUREFILTERTYPE { D3DTEXF_NONE = 0, D3DTEXF_POINT = 1, D3DTEXF_LINEAR = 2, D3DTEXF_ANISOTROPIC = 3, D3DTEXF_FLATCUBIC = 4, D3DTEXF_GAUSSIANCUBIC = 5 };
enum D3DCULL { D3DCULL_NONE = 1, D3DCULL_CW = 2, D3DCULL_CCW = 3 };
enum D3DFILLMODE { D3DFILL_POINT = 1, D3DFILL_WIREFRAME = 2, D3DFILL_SOLID = 3 };
enum D3DSHADEMODE { D3DSHADE_FLAT = 1, D3DSHADE_GOURAUD = 2 };
enum D3DFOGMODE { D3DFOG_NONE = 0, D3DFOG_EXP = 1, D3DFOG_EXP2 = 2, D3DFOG_LINEAR = 3 };
enum D3DPRIMITIVETYPE { D3DPT_POINTLIST = 1, D3DPT_LINELIST = 2, D3DPT_LINESTRIP = 3, D3DPT_TRIANGLELIST = 4, D3DPT_TRIANGLESTRIP = 5, D3DPT_TRIANGLEFAN = 6 };
enum D3DBACKBUFFER_TYPE { D3DBACKBUFFER_TYPE_MONO = 0, D3DBACKBUFFER_TYPE_LEFT = 1, D3DBACKBUFFER_TYPE_RIGHT = 2 };
enum D3DFORMAT { D3DFMT_UNKNOWN = 0, D3DFMT_R8G8B8 = 20, D3DFMT_A8R8G8B8 = 21, D3DFMT_X8R8G8B8 = 22, D3DFMT_R5G6B5 = 23, D3DFMT_A1R5G5B5 = 25, D3DFMT_A4R4G4B4 = 26, D3DFMT_A8 = 28, D3DFMT_D16 = 80, D3DFMT_D24S8 = 75 };

// Texture coordinate index flags (D3DTSS_TEXCOORDINDEX)
#define D3DTSS_TCI_PASSTHRU                    0x00000000
#define D3DTSS_TCI_CAMERASPACENORMAL           0x00010000
#define D3DTSS_TCI_CAMERASPACEPOSITION         0x00020000
#define D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR 0x00030000

// Texture transform flags (D3DTSS_TEXTURETRANSFORMFLAGS)
#define D3DTTFF_DISABLE   0
#define D3DTTFF_COUNT1    1
#define D3DTTFF_COUNT2    2
#define D3DTTFF_COUNT3    3
#define D3DTTFF_COUNT4    4
#define D3DTTFF_PROJECTED 256

// Colour write mask bits
#define D3DCOLORWRITEENABLE_RED   1
#define D3DCOLORWRITEENABLE_GREEN 2
#define D3DCOLORWRITEENABLE_BLUE  4
#define D3DCOLORWRITEENABLE_ALPHA 8

// Clear flags (D3DCLEAR_TARGET_A is the Xbox alpha-only clear)
#define D3DCLEAR_TARGET   0x00000001
#define D3DCLEAR_ZBUFFER  0x00000002
#define D3DCLEAR_STENCIL  0x00000004
#define D3DCLEAR_TARGET_A 0x00000010

// Flexible vertex format bits
#define D3DFVF_RESERVED0     0x001
#define D3DFVF_POSITION_MASK 0x00E
#define D3DFVF_XYZ           0x002
#define D3DFVF_XYZRHW        0x004
#define D3DFVF_XYZB1         0x006
#define D3DFVF_XYZB2         0x008
#define D3DFVF_XYZB3         0x00a
#define D3DFVF_XYZB4         0x00c
#define D3DFVF_XYZB5         0x00e
#define D3DFVF_NORMAL        0x010
#define D3DFVF_PSIZE         0x020
#define D3DFVF_DIFFUSE       0x040
#define D3DFVF_SPECULAR      0x080
#define D3DFVF_TEXCOUNT_MASK 0xf00
#define D3DFVF_TEXCOUNT_SHIFT 8
#define D3DFVF_TEX0          0x000
#define D3DFVF_TEX1          0x100
#define D3DFVF_TEX2          0x200
#define D3DFVF_TEX3          0x300
#define D3DFVF_TEX4          0x400
#define D3DFVF_TEXCOORDSIZE1(i) (3 << ((i) * 2 + 16))
#define D3DFVF_TEXCOORDSIZE2(i) (0)
#define D3DFVF_TEXCOORDSIZE3(i) (1 << ((i) * 2 + 16))
#define D3DFVF_TEXCOORDSIZE4(i) (2 << ((i) * 2 + 16))

typedef DWORD D3DCOLOR;
#define D3DCOLOR_ARGB(a, r, g, b) ((D3DCOLOR)((((a) & 0xff) << 24) | (((r) & 0xff) << 16) | (((g) & 0xff) << 8) | ((b) & 0xff)))
#define D3DCOLOR_RGBA(r, g, b, a) D3DCOLOR_ARGB(a, r, g, b)
#define D3DCOLOR_XRGB(r, g, b) D3DCOLOR_ARGB(0xff, r, g, b)

struct D3DRECT { long x1, y1, x2, y2; };

// Textures are engine textures.  A surface is a second handle on a texture's
// GPU resources (the game forward-declares `struct IDirect3DSurface8`), so
// a surface can be cast to a base texture and bound like one.  Surfaces are
// AddRef'd on hand-out like D3D's; Release() them.
typedef gfxTexture IDirect3DBaseTexture8;
typedef gfxTexture IDirect3DTexture8;
typedef gfxTexture IDirect3DCubeTexture8;
struct IDirect3DSurface8 : public gfxTexture {
	IDirect3DSurface8(gfxTexture *source);
	gfxTexture *Source;   // the texture this surface views (referenced)
};
typedef IDirect3DBaseTexture8 *LPDIRECT3DBASETEXTURE8;
typedef IDirect3DTexture8 *LPDIRECT3DTEXTURE8;
typedef IDirect3DSurface8 *LPDIRECT3DSURFACE8;

// --- The device -------------------------------------------------------------

class gfxD3DCompatDevice {
public:
	enum { NUM_STAGES = 4, NUM_RENDER_STATES = 256 };

	gfxD3DCompatDevice();

	// Back to D3D defaults (also re-applies them to RSTATE).
	void Reset();

	// Render states
	HRESULT SetRenderState(int state, DWORD value);
	HRESULT GetRenderState(int state, DWORD *value) const;

	// Texture stage / sampler states (either vocabulary)
	HRESULT SetTextureStageState(DWORD stage, int type, DWORD value);
	HRESULT GetTextureStageState(DWORD stage, int type, DWORD *value) const;
	HRESULT SetSamplerState(DWORD stage, int type, DWORD value) { return SetTextureStageState(stage, type, value); }

	HRESULT SetTexture(DWORD stage, IDirect3DBaseTexture8 *tex);
	HRESULT GetTexture(DWORD stage, IDirect3DBaseTexture8 **tex) const;

	// Vertex layout.  SetVertexShader accepts an FVF code (D3D8 idiom) or a
	// shader handle (ignored: fixed-function only).
	HRESULT SetFVF(DWORD fvf) { m_FVF = fvf; return D3D_OK; }
	HRESULT GetFVF(DWORD *fvf) const { if (fvf) *fvf = m_FVF; return D3D_OK; }
	HRESULT SetVertexShader(DWORD handle);
	HRESULT GetVertexShader(DWORD *handle) const { if (handle) *handle = m_VertexShader; return D3D_OK; }
	HRESULT SetPixelShader(DWORD handle) { m_PixelShader = handle; return D3D_OK; }
	HRESULT GetPixelShader(DWORD *handle) const { if (handle) *handle = m_PixelShader; return D3D_OK; }
	// Programmable-shader constants: the compat layer is fixed-function only
	// (every D3D8 shader the game created is Xbox-side); accepted and dropped.
	HRESULT SetVertexShaderConstant(DWORD, const void *, DWORD) { return D3D_OK; }
	HRESULT SetPixelShaderConstant(DWORD, const void *, DWORD) { return D3D_OK; }

	// Immediate draws from user memory laid out per the current FVF.
	HRESULT DrawPrimitiveUP(int primType, UINT primCount, const void *verts, UINT stride);
	HRESULT DrawVerticesUP(int primType, UINT vertexCount, const void *verts, UINT stride);   // Xbox spelling: a vertex count

	// D3D8 scene brackets: D3D11 has none (the frame is bracketed by
	// gfxBeginFrame / gfxEndFrame), so these only report success.
	HRESULT BeginScene() { return D3D_OK; }
	HRESULT EndScene() { return D3D_OK; }

	// Surfaces.  Returned surfaces are AddRef'd like D3D's; Release() them.
	// index 0 = the back buffer, -1 = the front buffer (both resolve to the
	// pipeline's backbuffer copy, refreshed for the front buffer).
	HRESULT GetBackBuffer(int index, int type, IDirect3DSurface8 **out);
	HRESULT GetRenderTarget(IDirect3DSurface8 **out);
	HRESULT GetDepthStencilSurface(IDirect3DSurface8 **out);
	HRESULT CreateImageSurface(UINT width, UINT height, int format, IDirect3DSurface8 **out);
	HRESULT CopyRects(IDirect3DSurface8 *src, const void *srcRects, UINT numRects, IDirect3DSurface8 *dst, const void *dstPoints);
	HRESULT Clear(DWORD count, const D3DRECT *rects, DWORD flags, D3DCOLOR color, float z, DWORD stencil);

	static int PrimitiveVertexCount(int primType, UINT primCount);
	static UINT FVFVertexSize(DWORD fvf);

private:
	struct Stage {
		DWORD ColorOp, ColorArg1, ColorArg2;
		DWORD AlphaOp, AlphaArg1, AlphaArg2;
		DWORD AddressU, AddressV, AddressW;
		DWORD MagFilter, MinFilter, MipFilter;
		DWORD TexCoordIndex, TransformFlags;
		float MipLodBias;
		gfxTexture *Texture;
	};

	void ApplyStage(int stage);
	void ApplyStencil();
	void ApplyAlphaTest();
	void ApplyBlend();
	void ApplyFog();
	void ApplyColorWrite();

	Stage m_Stage[NUM_STAGES];
	DWORD m_RenderState[NUM_RENDER_STATES];
	DWORD m_FVF;
	DWORD m_VertexShader;
	DWORD m_PixelShader;
};

// "The device" the game holds, plus the flags its D3D paths test.
extern gfxD3DCompatDevice *lpD3DDev;
extern DWORD gfxPixelShaders;      // 0: no pixel shader support (fixed-function branches)
extern bool gfxHardwareShaders;    // false: same for vertex shaders
extern bool gfxDisableNextStage;   // game-side flag: skip binding the next texture stage

// Free-function spellings used by the PC code paths.
inline HRESULT SetRenderState(int state, DWORD value) { return lpD3DDev->SetRenderState(state, value); }
inline HRESULT SetTextureStageState(DWORD stage, int type, DWORD value) { return lpD3DDev->SetTextureStageState(stage, type, value); }
inline HRESULT SetSamplerState(DWORD stage, int type, DWORD value) { return lpD3DDev->SetSamplerState(stage, type, value); }
inline HRESULT SetTexture(DWORD stage, IDirect3DBaseTexture8 *tex) { return lpD3DDev->SetTexture(stage, tex); }
inline HRESULT DrawPrimitiveUP(int primType, UINT primCount, const void *verts, UINT stride) { return lpD3DDev->DrawPrimitiveUP(primType, primCount, verts, stride); }

// D3D blend factor pair (+ op) -> engine blend set; unknown pairs report
// once and fall back to opaque (blendSet_One_Zero).
EnumBlendSet gfxD3DBlendSetFromPair(DWORD src, DWORD dst, DWORD op = D3DBLENDOP_ADD);

// ddtry(expr): evaluate a device call and report a failing HRESULT once per site.
HRESULT gfxD3DCheck(HRESULT hr, const char *expr, const char *file, int line);
#define ddtry(x) gfxD3DCheck((x), #x, __FILE__, __LINE__)

#endif // GFX_D3DCOMPAT_H
