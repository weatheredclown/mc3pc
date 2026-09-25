////////////////////////////////////////
// rgl.cpp
//
// Direct3D 11 backend for the AGE immediate-mode line renderer (rgl*), plus the
// device/swapchain lifecycle the app framework drives (gfxOpenDevice /
// gfxBeginFrame / gfxEndFrame / gfxCloseDevice) and a minimal render state
// (RSTATE camera).  This is the first visible rung: a cleared window with a
// world-space grid + axes.
////////////////////////////////////////

#include "data/args.h" // -shot auto-screenshot
#include "rmcore/light.h"
#include "data/timemgr.h"
#include "gfx/rstate.h"
#include "gfx/texture.h"
#include "gfx/bitmap.h"
#include "gfx/vgl.h"
#include "gfx/gputimer.h"  // -gputime per-pass GPU timing
#include "gfx/font.h" // gfxFontGetWidth/Height for vglDrawLabel cell offsets
#include "input/keyboard.h"
#include "input/mouse.h" // per-frame input edge reset in gfxEndFrame
#include "vector/amath.h"
#include <stdlib.h>

#include "atl/array.h"
#include "simple.h"
#include <DirectXMath.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <windows.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

#include <gdiplus.h>
#include <objidl.h>
#pragma comment(lib, "gdiplus.lib")

using namespace DirectX;

////////////////////////////////////////////////////////////////////////////
// Device state

static ID3D11Device *sDevice;
static ID3D11DeviceContext *sCtx;
static IDXGISwapChain *sSwap;
static ID3D11RenderTargetView *sRTV;
static ID3D11DepthStencilView *sDSV;
static ID3D11VertexShader *sVS;
static ID3D11PixelShader *sPS;
static ID3D11InputLayout *sLayout;
static ID3D11Buffer *sVB; // dynamic vertex buffer
static ID3D11Buffer *sCB; // constant buffer (mvp)
static int sWidth, sHeight;
static ID3D11ShaderResourceView *sDefaultWhiteSRV;
static ID3D11SamplerState *sSampler;

static const int MAX_VERTS = 65536;

struct LineVertex {
  float x, y, z;
  float r, g, b, a;
  float u, v;
  float u2, v2;   // second UV set (lightmap/detail stage)
  float nx, ny, nz; // model-space normal (lit path)
};

// Immediate-mode accumulation
static atArray<LineVertex> sVerts;
static gfxPackedColor sCurColor = 0xffffffffu;
static float sCurU = 0.0f, sCurV = 0.0f;
static float sCurU2 = 0.0f, sCurV2 = 0.0f;
static float sCurNx = 0.0f, sCurNy = 1.0f, sCurNz = 0.0f;
// Per texture stage: which vertex UV set feeds it (0 = tex, 1 = tex2).  Stage 1
// (the model-side lightmap stage) defaults to the second set.
static int sTexSrc[2] = {0, 1};
// Vertex lighting only applies to batches that supplied normals (model draws).
// Immediate-mode FX (sprites, lasers, glows, lines) never call vglNormal3f and
// must stay unlit -- otherwise they get modulated by whatever light group the
// last entity bound, which strobes.
static bool sLitBatch = false;
static gfxTexture *sBoundTex = NULL;
// Stage-2 combine (vglTex2Combine / vglTex2CombineOps): how the stage-2 sample
// joins the stage-1 result.  Colour: tex2colModulate/Decal/Current/Texture;
// alpha: tex2alphaModulate/Current/Texture.  Sent to the shader as
// gTexMisc.z (1 + colour op, 0 = no stage 2) and gTexGen.w (alpha op).
static int sTex2ColorOp = tex2colModulate;
static int sTex2AlphaOp = tex2alphaModulate;
static float sTexColorScale = 1.0f;   // texopModulate2X = 2
static XMMATRIX sTexMtx = XMMatrixIdentity();   // stage-0 texture matrix (SetTexMatrix)
static XMMATRIX sTexMtx2 = XMMatrixIdentity();  // stage-1 texture matrix (SetTexMatrix(1, ...))
static int sTexGen[2] = {0, 0};                 // 1 = apply sTexMtx / sTexMtx2 (SetTexGeneration mode 2)
static int sStencilMode = 0;                    // SetStencilMode (0 off, 1/2 shadow presets, 3 = sStencil)
static gfxStencilState sStencil;                // SetStencil (explicit state, mode 3)
static u32 sTexFactor = 0xffffffffu;            // SetTextureFactor (recorded)
static gfxTextureOp sTexAlphaOp[2] = {texopModulate, texopModulate};   // SetTexAlphaOp (recorded)
// Per-stage sampler selection (SetTextureAddress/Filter/MipLodBias) + lazily built states.
struct SamplerKey { u8 addrU, addrV, linear, mip; float bias; };
static SamplerKey sSamplerKey[2] = {{0, 0, 1, 1, 0.0f}, {0, 0, 1, 1, 0.0f}};
static struct { SamplerKey key; ID3D11SamplerState *state; } sSamplerCache[32];
static int sNumSamplers = 0;
// Explicit-stencil depth states (mode 3), keyed by depth key + stencil state.
static struct { int depthKey; gfxStencilState st; ID3D11DepthStencilState *state; } sCustomDepth[32];
static int sNumCustomDepth = 0;
static void (*sCTFFunc)() = NULL;               // copy-to-front hook (fxCopyToFront)
static bool sCTFActive = false;                 // inside the hook: PS2 blits are y-flipped, PC copies are not
static gfxTexture *sBackCopy = NULL;            // living copy of the backbuffer
static bool sShotSuspended = false;             // gfxSetShotSuspended (loading pump)
static bool sShotLoadingFrame = false;          // set by gfxDrawLoadingBackdrop, cleared each frame
static int  sSceneDrawsThisFrame = 0;           // 3D submissions this frame (0 = a 2D-only frame)
static bool sShotLoadDone = false;              // -shotload: capture one loading frame
static ID3D11Texture2D *sBackCopyTex = NULL;    // its D3D resource (CopyResource target)
static gfxTexture *sBoundTex2 = NULL;   // stage 2; white (no-op modulate) when NULL
// gfxRenderState::SetTextureAlphaBlendAllowed: false while the caller has picked an
// opaque (ONE/ZERO) blend set; the draw-time texture-alpha overrides must then leave the
// blend and depth state alone (city ground/main passes: sidewalk alpha is a reflection mask).
static bool sTexAlphaBlendAllowed = true;
static XMMATRIX sWorld, sView, sProj;

// Blend state: the current (src, dest, op) factor triple -- set as a preset
// by SetBlendSet or one factor at a time by SetSrcBlend/SetDestBlend/
// SetBlendOp -- plus a lazily-built D3D11 state cache keyed on the triple,
// the colour write mask and the enable.  When blending is disabled we bind an
// opaque state with the same write mask.
static bool sBlendEnable = false;
static EnumBlendSet sBlendSet = blendSet_One_Zero;
static gfxBlendFunc sBlendSrc = blendOne;
static gfxBlendFunc sBlendDst = blendZero;
static int sBlendOp = blendOpAdd;
static struct { u32 key; ID3D11BlendState *state; } sBlendCache[64];
static int sNumBlendStates = 0;
// Scissor rectangle (gfxRenderState::SetScissor), screen pixels.
static bool sScissorEnable = false;
static int sScissorX = 0, sScissorY = 0, sScissorW = 0, sScissorH = 0;
static int sFogMode = fogLinear;              // gfxFogMode (fogNone = off)
static int sVglFormat = 0;                    // vglSetFormat (eFVF), recorded
static int sAlphaFail = afailKeep;            // gfxRenderState::SetAlphaFail
// Blit2D / ClearRect / stroke text: raw screen-space quads.  They ignore the
// RSTATE world matrix (PS2 sprites did), unlike vgl batches drawn while an
// ortho viewport is current (HUD needles: RSTATE.SetWorld rotates them).
static bool sRaw2D = false;

// Depth-stencil + rasterizer render state, driven by RSTATE.  All geometry
// (grid, bones, models, particles) funnels through FlushLines, so applying the
// state here configures the whole immediate-mode pipeline uniformly.  States
// are lazily built and cached: depth by (ztest|zwrite|zfunc), rasterizer by
// cull.
static bool sZTestEnable = true;
static bool sZWriteEnable = true;
static EnumZFunc sZFunc = zLEqual;
static float sZBias = 0.0f;
static bool sFogEnable = false;
static float sFogColor[3] = {0.3f, 0.3f, 0.3f};
static float sFogStart = 0.0f, sFogEnd = 100.0f, sFogMin = 0.0f, sFogMax = 1.0f;
static float sFogPower = 0.0f; // 0 = linear (per-vertex fog), >0 = palette-fog curve
static float sLightAmbient[4] = {1.0f, 1.0f, 1.0f, 0.0f};
static float sLightDir[3][4] = {{0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}};   // w = 1 directional, 2 point (xyz = pos)
static float sLightCol[3][4] = {{0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}};   // rgb * intensity
static gfxCullMode sCullMode = cullNone;
static ID3D11DepthStencilState *sDepthCache[99] = {};   // 33 depth keys x 3 stencil modes
static ID3D11RasterizerState *sRasterCache[16] = {};

// Colour write mask (SetColorMask) folds into the blend state's write mask; a
// texturing toggle (SetTexEnable) forces the default white texture when off.
static UINT8 sColorMask = D3D11_COLOR_WRITE_ENABLE_ALL; // R|G|B|A
static bool sTexEnable = true;
static bool sOrthoMode = false;
// vglOrtho(true): screen pixels over the viewport window, even when the
// current viewport carries its own Ortho2D range (the HUD map's background).
static bool sScreenOrtho = false;
// A camera was set since the viewport last changed.  An Ortho2D viewport then
// draws world * view * its range (the HUD map rotates its ring and projects
// the city through the camera); without one it stays 2D (agro effect fan).
static bool sOrthoVPCamera = false;

// Alpha test (shader clip): enabled while func != alphaAlways.  Ref is 0..1.
static int sAlphaFunc = alphaAlways;
static float sAlphaRef = 0.0f;

// The blend cache is keyed on the colour mask too, so a mask change needs no
// invalidation; kept for the device-close path.
static void InvalidateBlendCache() {
  for (int i = 0; i < sNumBlendStates; i++)
    if (sBlendCache[i].state) {
      sBlendCache[i].state->Release();
      sBlendCache[i].state = NULL;
    }
  sNumBlendStates = 0;
}

static const char *kShader =
    // gAlphaTest = (ref[0..1], func[0..7], enable, unused).  func matches
    // EnumZFunc- style gfxAlphaFunc: 0 Never 1 Less 2 Equal 3 LEqual 4 Greater
    // 5 NotEqual 6 GEqual 7 Always.  RSTATE.SetAlphaFunc/SetAlphaRef drive it.
    "cbuffer CB : register(b0){ row_major float4x4 gMVP; float4 gAlphaTest;"
    "  row_major float4x4 gWV; float4 gFogParams; float4 gFogColor; float4 gFogMisc;"
    "  row_major float4x4 gWorld; float4 gLightMisc; float4 gAmbient; float4 gLightDir[3]; float4 gLightCol[3];"
    "  float4 gTexMisc; row_major float4x4 gTexMtx; float4 gTexGen; row_major float4x4 gTexMtx2; };"  // gTexMisc x,y: UV set for stage 0/1; z: stage-2 colour op; gTexGen.w: stage-2 alpha op
    "struct VSIn{ float3 pos:POSITION; float4 col:COLOR; float2 tex:TEXCOORD0; "
    "float2 tex2:TEXCOORD1; float3 nrm:NORMAL; };"
    "struct VSOut{ float4 pos:SV_POSITION; float4 col:COLOR; float2 "
    "tex:TEXCOORD0; float2 tex2:TEXCOORD1; float vz:TEXCOORD2; };"
    // Vertex lighting (PS2 rv1 style): ambient + up to 3 directional/point lights,
    // modulating the vertex colour.  gLightMisc.x enables it (RSTATE lighting on
    // and a light group bound); gLightDir[i].w: 1 = directional (xyz = direction
    // the light travels), 2 = point (xyz = world position).
    // gTexGen.x (stage 0): 1 = UV * gTexMtx (SetTexMatrix); 2 = sphere map from
    // the view-space normal (texsrcCameraSpaceNormal); 3 = sphere map from the
    // view-space reflection vector (texsrcCameraSpaceReflectionVector) -- the
    // fixed-function environment-map modes gfxModel::DrawEnvMapped relies on.
    "VSOut VS(VSIn i){ VSOut o; o.pos=mul(float4(i.pos,1),gMVP); o.col=i.col; "
    "o.tex=i.tex; o.tex2=i.tex2; float4 vp = mul(float4(i.pos,1),gWV); o.vz=abs(vp.z);"
    "  if (gTexGen.y == 1.0) o.tex2 = mul(float4(i.tex2,1.0,0.0), gTexMtx2).xy;"
    "  else if (gTexGen.y >= 2.0){"
    "    float3 nv2 = normalize(mul(float4(i.nrm,0),gWV).xyz);"
    "    if (gTexGen.y >= 3.0){ float3 e2 = normalize(vp.xyz); float3 r2 = reflect(e2, nv2);"
    "      float m2 = 2.0 * sqrt(r2.x*r2.x + r2.y*r2.y + (r2.z - 1.0)*(r2.z - 1.0)); nv2 = r2 / max(m2, 1e-4); nv2.xy += 0.5; o.tex2 = float2(nv2.x, 1.0 - nv2.y); }"
    "    else o.tex2 = float2(nv2.x * 0.5 + 0.5, 0.5 - nv2.y * 0.5);"
    "  }"
    "  if (gTexGen.x == 1.0) o.tex = mul(float4(i.tex,1.0,0.0), gTexMtx).xy;"
    "  else if (gTexGen.x >= 2.0){"
    "    float3 nv = normalize(mul(float4(i.nrm,0),gWV).xyz);"
    "    if (gTexGen.x >= 3.0){ float3 e = normalize(vp.xyz); float3 r = reflect(e, nv);"
    "      float m = 2.0 * sqrt(r.x*r.x + r.y*r.y + (r.z - 1.0)*(r.z - 1.0)); nv = r / max(m, 1e-4); nv.xy += 0.5; o.tex = float2(nv.x, 1.0 - nv.y); }"
    "    else o.tex = float2(nv.x * 0.5 + 0.5, 0.5 - nv.y * 0.5);"
    "  }"
    "  if (gLightMisc.x != 0.0){"
    "    float3 wp = mul(float4(i.pos,1),gWorld).xyz;"
    "    float3 n = normalize(mul(float4(i.nrm,0),gWorld).xyz);"
    "    float3 lit = gAmbient.rgb;"
    "    [unroll] for (int k = 0; k < 3; k++){"
    "      if (gLightDir[k].w == 1.0) lit += gLightCol[k].rgb * saturate(dot(n, -normalize(gLightDir[k].xyz)));"
    "      else if (gLightDir[k].w == 2.0){ float3 d = gLightDir[k].xyz - wp; float dd = max(dot(d,d), 1e-3);"
    "        lit += gLightCol[k].rgb * saturate(gLightCol[k].w / dd) * saturate(dot(n, d * rsqrt(dd))); }"  // lgtLight::ComputeIntensity: Intensity/dist^2 (matches the PS2 look: dark floors, silhouetted characters)
    "      else if (gLightDir[k].w == 3.0) lit += gLightCol[k].rgb * saturate(dot(n, normalize(gLightDir[k].xyz - wp)));"
    "    }"
    "    o.col.rgb *= min(lit, 2.0);"  // GS packs col*lit into a byte: overbright up to 0xFF = ~2x
    "  }"
    "  return o; }"
    "Texture2D shaderTexture : register(t0);"
    "Texture2D shaderTexture2 : register(t1);"   // stage 2: white SRV when unbound
    "SamplerState sampleState : register(s0);"
    "float4 PS(VSOut i):SV_TARGET{"
    "  float2 uvA = (gTexMisc.x >= 1.0) ? i.tex2 : i.tex;"
    "  float2 uvB = (gTexMisc.y >= 1.0) ? i.tex2 : i.tex;"
    // GS modulate: vertex colour is a byte clamped at 0xFF, i.e. min(col *
    // scale, scale) with scale = 255/128 on textured draws (gTexMisc.w).
    "  float4 vc = i.col;"
    "  if (gTexMisc.w > 0.0) vc.rgb = min(vc.rgb * gTexMisc.w, gTexMisc.w);"
    "  if (gTexGen.z > 0.0) vc.a = min(vc.a * gTexGen.z, gTexGen.z);"
    "  float4 t0 = shaderTexture.Sample(sampleState, uvA); float4 c = vc * t0;"
    // Stage 2 (gTexMisc.z = 1 + colour op, 0 = unbound): modulate / decal
    // (lerp by the stage-2 alpha) / keep the stage-1 result / take the
    // stage-2 colour; gTexGen.w = alpha op: modulate / keep / take.
    "  if (gTexMisc.z >= 1.0){ float4 t2 = shaderTexture2.Sample(sampleState, uvB); float4 c0 = c;"
    "    if (gTexMisc.z < 1.5) c.rgb *= t2.rgb;"
    "    else if (gTexMisc.z < 2.5) c.rgb = lerp(c.rgb, t2.rgb, t2.a);"
    "    else if (gTexMisc.z < 3.5) c.rgb = c0.rgb;"
    "    else if (gTexMisc.z < 4.5) c.rgb = t2.rgb;"
    // op 4: add the stage-2 colour weighted by the stage-1 TEXTURE alpha (the PS2's
    // "ps2blend Cs 0 Ad Cd" second pass over a base that wrote its alpha: city windows)
    "    else if (gTexMisc.z < 5.5) c.rgb = c0.rgb + t2.rgb * t0.a;"
    // op 5: decal of the vertex-coloured stage-2 texel by its own alpha (a PS2 "blendset
    // normal" second pass that keeps the CPV colour: city_road's detail / grime layer)
    "    else if (gTexMisc.z < 6.5) c.rgb = lerp(c0.rgb, vc.rgb * t2.rgb, t2.a);"
    // op 6: add the stage-2 colour weighted by the VERTEX alpha - car paint reflecting
    // the live city environment map, with the per-vertex shading supplying the fresnel
    // weight (gfx/model.cpp sCarShade).
    // No modulate boost here: the weight this is multiplied by was calibrated
    // against a mid-grey environment constant, and the map it now samples spans
    // 0..1, so boosting it as well blew the sky-facing panels out to white.
    "    else c.rgb = c0.rgb + t2.rgb * vc.a;"
    "    if (gTexGen.w < 0.5) c.a *= t2.a; else if (gTexGen.w < 1.5) c.a = c0.a; else c.a = t2.a; }"
    "  if (gAlphaTest.z != 0.0){"
    "    float a = c.a, r = gAlphaTest.x; int f = (int)gAlphaTest.y; bool "
    "bPass = true;"
    "    const float e = 0.5/255.0;"
    "    if      (f==0) bPass = false;"
    "    else if (f==1) bPass = a <  r;"
    "    else if (f==2) bPass = abs(a-r) <= e;"
    "    else if (f==3) bPass = a <= r + e;"
    "    else if (f==4) bPass = a >  r;"
    "    else if (f==5) bPass = abs(a-r) >  e;"
    "    else if (f==6) bPass = a >= r - e;"
    "    if (!bPass) discard;"
    "  }"
    // Distance fog: gFogParams = (start, end, min, max), gFogColor = (rgb, power),
    // gFogMisc.x = enable, gFogMisc.y = gfxFogMode.  power > 0 reproduces
    // lvlFogData::CreatePalette's curve (fog = 1 - pow(1 - t, power)); fogExp /
    // fogExp2 use density = 1 / (end - start); else the per-vertex linear ramp.
    "  if (gFogMisc.x != 0.0){"
    "    float range = max(gFogParams.y - gFogParams.x, 1e-3);"
    "    float t = saturate((i.vz - gFogParams.x) / range);"
    "    float f = (gFogColor.w > 0.0) ? (1.0 - pow(max(1.0 - t, 1e-4), gFogColor.w)) : t;"
    "    if (gFogMisc.y == 2.0) f = 1.0 - exp(-max(i.vz - gFogParams.x, 0.0) / range);"
    "    else if (gFogMisc.y == 3.0){ float d = max(i.vz - gFogParams.x, 0.0) / range; f = 1.0 - exp(-d * d); }"
    "    f = clamp(f, gFogParams.z, gFogParams.w);"
    "    c.rgb = lerp(c.rgb, gFogColor.rgb, f);"
    "  }"
    "  return c;"
    "}";

////////////////////////////////////////////////////////////////////////////
// Helpers

static void ColorToFloats(gfxPackedColor c, float &r, float &g, float &b,
                          float &a) {
  a = ((c >> 24) & 0xff) / 255.0f;
  r = ((c >> 16) & 0xff) / 255.0f;
  g = ((c >> 8) & 0xff) / 255.0f;
  b = (c & 0xff) / 255.0f;
}

// AGE Matrix34 (rows a,b,c = 3x3, d = translation) -> row-major XMMATRIX.
static XMMATRIX ToXM(const Matrix34 &m) {
  return XMMATRIX(m.a.x, m.a.y, m.a.z, 0.0f, m.b.x, m.b.y, m.b.z, 0.0f, m.c.x,
                  m.c.y, m.c.z, 0.0f, m.d.x, m.d.y, m.d.z, 1.0f);
}

////////////////////////////////////////////////////////////////////////////
// Device lifecycle (called by the app framework)

extern "C" void gfxOpenDevice(void *hwnd, int w, int h) {
  sWidth = w;
  sHeight = h;

  DXGI_SWAP_CHAIN_DESC sd = {};
  sd.BufferCount = 1;
  sd.BufferDesc.Width = w;
  sd.BufferDesc.Height = h;
  sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.OutputWindow = (HWND)hwnd;
  sd.SampleDesc.Count = 1;
  sd.Windowed = TRUE;

  D3D_FEATURE_LEVEL fl;
  HRESULT hr = D3D11CreateDeviceAndSwapChain(
      NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &sd,
      &sSwap, &sDevice, &fl, &sCtx);
  if (FAILED(hr)) {
    // Fallback to WARP (software rendering) if hardware driver is not available
    // (e.g. in VM/sandbox).
    hr = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_WARP, NULL, 0,
                                       NULL, 0, D3D11_SDK_VERSION, &sd, &sSwap,
                                       &sDevice, &fl, &sCtx);
  }
  if (FAILED(hr) || !sSwap) {
    MessageBoxA(NULL,
                "Failed to create D3D11 device (both hardware and WARP "
                "software driver failed).",
                "D3D11 Error", MB_OK | MB_ICONERROR);
    exit(1);
  }

  // Backbuffer render target.
  printf("[D3D11] Getting backbuffer...\n");
  fflush(stdout);
  ID3D11Texture2D *back = NULL;
  sSwap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&back);
  printf("[D3D11] Creating RenderTargetView...\n");
  fflush(stdout);
  sDevice->CreateRenderTargetView(back, NULL, &sRTV);
  back->Release();

  // Depth buffer.
  printf("[D3D11] Creating depth buffer...\n");
  fflush(stdout);
  D3D11_TEXTURE2D_DESC dd = {};
  dd.Width = w;
  dd.Height = h;
  dd.MipLevels = 1;
  dd.ArraySize = 1;
  dd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
  dd.SampleDesc.Count = 1;
  dd.Usage = D3D11_USAGE_DEFAULT;
  dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
  ID3D11Texture2D *depth = NULL;
  sDevice->CreateTexture2D(&dd, NULL, &depth);
  sDevice->CreateDepthStencilView(depth, NULL, &sDSV);
  depth->Release();

  // Shaders.
  printf("[D3D11] Compiling VS...\n");
  fflush(stdout);
  ID3DBlob *vsb = NULL, *psb = NULL, *err = NULL;
  HRESULT hrVS = D3DCompile(kShader, strlen(kShader), NULL, NULL, NULL, "VS",
                            "vs_4_0", 0, 0, &vsb, &err);
  if (FAILED(hrVS) || !vsb) {
    if (err) {
      printf("[D3D11] VS compile error: %s\n", (char *)err->GetBufferPointer());
      fflush(stdout);
    }
    MessageBoxA(NULL, "VS compile failed", "D3D11 Shader Error", MB_OK);
    exit(1);
  }

  printf("[D3D11] Compiling PS...\n");
  fflush(stdout);
  HRESULT hrPS = D3DCompile(kShader, strlen(kShader), NULL, NULL, NULL, "PS",
                            "ps_4_0", 0, 0, &psb, &err);
  if (FAILED(hrPS) || !psb) {
    if (err) {
      printf("[D3D11] PS compile error: %s\n", (char *)err->GetBufferPointer());
      fflush(stdout);
    }
    MessageBoxA(NULL, "PS compile failed", "D3D11 Shader Error", MB_OK);
    exit(1);
  }

  printf("[D3D11] Creating VS and PS...\n");
  fflush(stdout);
  sDevice->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(),
                              NULL, &sVS);
  sDevice->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(),
                             NULL, &sPS);

  printf("[D3D11] Creating InputLayout...\n");
  fflush(stdout);
  D3D11_INPUT_ELEMENT_DESC il[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 28,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, 36,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 44,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
  };
  sDevice->CreateInputLayout(il, (UINT)(sizeof(il) / sizeof(il[0])), vsb->GetBufferPointer(),
                             vsb->GetBufferSize(), &sLayout);
  vsb->Release();
  psb->Release();

  // Create 1x1 default white texture
  printf("[D3D11] Creating default white texture...\n");
  fflush(stdout);
  uint32_t whitePixel = 0xffffffff;
  D3D11_TEXTURE2D_DESC wtd = {};
  wtd.Width = 1;
  wtd.Height = 1;
  wtd.MipLevels = 1;
  wtd.ArraySize = 1;
  wtd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  wtd.SampleDesc.Count = 1;
  wtd.Usage = D3D11_USAGE_DEFAULT;
  wtd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  ID3D11Texture2D *whiteTex = NULL;
  D3D11_SUBRESOURCE_DATA wData = {&whitePixel, 4, 4};
  sDevice->CreateTexture2D(&wtd, &wData, &whiteTex);
  sDevice->CreateShaderResourceView(whiteTex, NULL, &sDefaultWhiteSRV);
  if (whiteTex)
    whiteTex->Release();

  // Create sampler state
  printf("[D3D11] Creating SamplerState...\n");
  fflush(stdout);
  D3D11_SAMPLER_DESC sampDesc = {};
  sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
  sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
  sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
  sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
  sampDesc.MinLOD = 0;
  sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
  sDevice->CreateSamplerState(&sampDesc, &sSampler);

  // Dynamic vertex buffer + constant buffer.
  D3D11_BUFFER_DESC vb = {};
  vb.ByteWidth = sizeof(LineVertex) * MAX_VERTS;
  vb.Usage = D3D11_USAGE_DYNAMIC;
  vb.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  vb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  sDevice->CreateBuffer(&vb, NULL, &sVB);

  D3D11_BUFFER_DESC cb = {};
  cb.ByteWidth = sizeof(XMMATRIX) * 5 + 16 * 14; // gMVP..gTexGen + gTexMtx2
  cb.Usage = D3D11_USAGE_DYNAMIC;
  cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  sDevice->CreateBuffer(&cb, NULL, &sCB);

// --- Coordinate handedness: converted once here, at the D3D boundary. ---
// AGE/OpenGL is right-handed; DirectX defaults left-handed.  Doing the
// conversion here (view + projection + winding) keeps every C++ layer and
// asset in AGE-native space — no per-asset negations at load.  Flip
// AGE_RENDER_RH to 0 to A/B the mirror against a text-bearing model.
#ifndef AGE_RENDER_RH
#define AGE_RENDER_RH                                                          \
  0 // 1 = AGE/OpenGL right-handed (native); 0 = legacy D3D LH (renders kno; RH
    // clipped it out)
#endif

  D3D11_RASTERIZER_DESC rd = {};
  rd.FillMode = D3D11_FILL_SOLID;
  rd.CullMode = D3D11_CULL_NONE;
  // SetCamera's view no longer mirrors screen X, so world-space CCW winding
  // survives to the screen -- front faces are CCW (AGE/OpenGL native).
  rd.FrontCounterClockwise = TRUE;
  rd.DepthClipEnable = TRUE;
  ID3D11RasterizerState *rs = NULL;
  sDevice->CreateRasterizerState(&rd, &rs);
  sCtx->RSSetState(rs);

  sWorld = XMMatrixIdentity();
#if AGE_RENDER_RH
  sView = XMMatrixLookAtRH(XMVectorSet(4, 3, -6, 1), XMVectorZero(),
                           XMVectorSet(0, 1, 0, 0));
  sProj = XMMatrixPerspectiveFovRH(XMConvertToRadians(60.0f),
                                   (float)w / (float)h, 0.1f, 10000.0f);
#else
  sView = XMMatrixLookAtLH(XMVectorSet(4, 3, -6, 1), XMVectorZero(),
                           XMVectorSet(0, 1, 0, 0));
  sProj = XMMatrixPerspectiveFovLH(XMConvertToRadians(60.0f),
                                   (float)w / (float)h, 0.1f, 10000.0f);
#endif
  if (!SYSFONT_ptr) {
    static gfxFont defaultFont;
    SYSFONT_ptr = &defaultFont;
  }
}

static int sRTTSavedWidth = 0, sRTTSavedHeight = 0;
static bool sInRTT = false;
// The depth-stencil view bound for the current render-to-texture, if the
// caller asked for one (see gfxBeginRenderToTexture below).
static struct ID3D11DepthStencilView *sRTTBoundDSV;

extern "C" void gfxBeginFrame(unsigned clearColor) {
  gfxGpuTimerBeginFrame();
  if (sInRTT) {
    sInRTT = false;
    sRTTBoundDSV = NULL;
    if (sRTTSavedWidth > 0 && sRTTSavedHeight > 0) {
      sWidth = sRTTSavedWidth;
      sHeight = sRTTSavedHeight;
    }
  }
  float c[4];
  ColorToFloats(clearColor ? clearColor : 0xff203040u, c[0], c[1], c[2], c[3]);
  // The frame's ALPHA channel is a mask the renderer writes on purpose, not a
  // colour: the reflecting-ground pass writes 1 so that
  // mcCityEnvMap::CopyReflectedObjectsToFrame can blend the reflection through
  // it, and the HDR pass writes 1 for the bloom.  Clearing it opaque said
  // "every pixel reflects", which laid the city's neon across the sky.
  // -clearalphaopaque restores the old behaviour.
  static const bool sClearAlphaOpaque = ARGS.Get("clearalphaopaque") != NULL;
  if (!sClearAlphaOpaque) c[3] = 0.0f;
  sCtx->OMSetRenderTargets(1, &sRTV, sDSV);
  sCtx->ClearRenderTargetView(sRTV, c);
  sTexGen[0] = sTexGen[1] = 0;   // per-draw texture matrices never outlive a frame
  sTexColorScale = 1.0f;
  sStencilMode = 0;
  sCtx->ClearDepthStencilView(sDSV, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
  D3D11_VIEWPORT vp = {0, 0, (float)sWidth, (float)sHeight, 0, 1};
  sCtx->RSSetViewports(1, &vp);
}

static void sDrawLogClear(unsigned flags, bool inRTT);   // -drawlog census entry (defined with the census state)

extern "C" void gfxClear(unsigned flags, unsigned clearColor) {
  sDrawLogClear(flags, sInRTT);
  ID3D11RenderTargetView *rtv = sInRTT ? NULL : sRTV;
  ID3D11DepthStencilView *dsv = sInRTT ? NULL : sDSV;
  if (sInRTT) {
    sCtx->OMGetRenderTargets(1, &rtv, &dsv);
  }
  if ((flags & 1) && rtv) { // clearColor
    float c[4];
    ColorToFloats(clearColor, c[0], c[1], c[2], c[3]);
    sCtx->ClearRenderTargetView(rtv, c);
  }
  if ((flags & 2) && dsv) { // clearZ
    sCtx->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
  } else if ((flags & 4) && dsv) { // clearStencil
    sCtx->ClearDepthStencilView(dsv, D3D11_CLEAR_STENCIL, 1.0f, 0);
  }
  if (sInRTT) {
    if (rtv) rtv->Release();
    if (dsv) dsv->Release();
  }
}

// Grab the current backbuffer (call before Present) and write it as a PNG.
// Used by the -shot auto-screenshot path so captures don't depend on the OS
// window being foreground/unoccluded.
static int sGetEncoderClsid(const WCHAR *mime, CLSID *out) {
  UINT num = 0, size = 0;
  Gdiplus::GetImageEncodersSize(&num, &size);
  if (size == 0)
    return -1;
  Gdiplus::ImageCodecInfo *info = (Gdiplus::ImageCodecInfo *)malloc(size);
  if (!info)
    return -1;
  Gdiplus::GetImageEncoders(num, size, info);
  int found = -1;
  for (UINT j = 0; j < num; j++)
    if (wcscmp(info[j].MimeType, mime) == 0) {
      *out = info[j].Clsid;
      found = (int)j;
      break;
    }
  free(info);
  return found;
}

// CPU readback of a backbuffer rectangle into tightly packed RGBA8 rows
// (top-down, alpha forced opaque).  Backs pipeManager::Readback.  The
// rectangle is clamped to the backbuffer; returns false if nothing was read.
extern "C" bool gfxReadbackBackBuffer(unsigned char *rgba, int x, int y, int w, int h) {
  if (!sSwap || !sDevice || !sCtx || !rgba || w <= 0 || h <= 0)
    return false;

  ID3D11Texture2D *back = NULL;
  if (FAILED(sSwap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&back)) || !back)
    return false;
  D3D11_TEXTURE2D_DESC d;
  back->GetDesc(&d);

  D3D11_TEXTURE2D_DESC sd = d;
  sd.Usage = D3D11_USAGE_STAGING;
  sd.BindFlags = 0;
  sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  sd.MiscFlags = 0;
  ID3D11Texture2D *stg = NULL;
  if (FAILED(sDevice->CreateTexture2D(&sd, NULL, &stg)) || !stg) {
    back->Release();
    return false;
  }
  sCtx->CopyResource(stg, back);

  bool ok = false;
  D3D11_MAPPED_SUBRESOURCE ms;
  if (SUCCEEDED(sCtx->Map(stg, 0, D3D11_MAP_READ, 0, &ms))) {
    const int bw = (int)d.Width, bh = (int)d.Height;
    for (int row = 0; row < h; row++) {
      unsigned char *dst = rgba + (size_t)row * w * 4;
      const int sy = y + row;
      if (sy < 0 || sy >= bh) { memset(dst, 0, (size_t)w * 4); continue; }
      const unsigned char *src = (const unsigned char *)ms.pData + (size_t)sy * ms.RowPitch;
      for (int col = 0; col < w; col++) {
        const int sx = x + col;
        if (sx < 0 || sx >= bw) { dst[col * 4 + 0] = dst[col * 4 + 1] = dst[col * 4 + 2] = 0; dst[col * 4 + 3] = 255; continue; }
        dst[col * 4 + 0] = src[sx * 4 + 0];   // backbuffer is R8G8B8A8
        dst[col * 4 + 1] = src[sx * 4 + 1];
        dst[col * 4 + 2] = src[sx * 4 + 2];
        dst[col * 4 + 3] = 255;
      }
    }
    sCtx->Unmap(stg, 0);
    ok = true;
  }
  stg->Release();
  back->Release();
  return ok;
}

extern "C" void gfxSaveScreenshot(const char *path) {
  if (!sSwap || !sDevice || !sCtx || !path)
    return;

  ID3D11Texture2D *back = NULL;
  if (FAILED(sSwap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&back)) ||
      !back)
    return;
  D3D11_TEXTURE2D_DESC d;
  back->GetDesc(&d);

  // Staging copy we can read on the CPU.
  D3D11_TEXTURE2D_DESC sd = d;
  sd.Usage = D3D11_USAGE_STAGING;
  sd.BindFlags = 0;
  sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  sd.MiscFlags = 0;
  ID3D11Texture2D *stg = NULL;
  if (FAILED(sDevice->CreateTexture2D(&sd, NULL, &stg)) || !stg) {
    back->Release();
    return;
  }
  sCtx->CopyResource(stg, back);

  D3D11_MAPPED_SUBRESOURCE ms;
  if (SUCCEEDED(sCtx->Map(stg, 0, D3D11_MAP_READ, 0, &ms))) {
    Gdiplus::GdiplusStartupInput gi;
    ULONG_PTR tok;
    Gdiplus::GdiplusStartup(&tok, &gi, NULL);
    {
      int w = (int)d.Width, h = (int)d.Height;
      Gdiplus::Bitmap bmp(w, h, PixelFormat32bppARGB);
      Gdiplus::Rect rc(0, 0, w, h);
      Gdiplus::BitmapData bd;
      // -shotalpha: capture the frame's ALPHA channel as grey instead of its
      // colour.  The frame's alpha is a mask the renderer writes on purpose --
      // the reflecting ground writes 1, the buildings write 0, and
      // CopyReflectedObjectsToFrame blends the reflection through it -- and
      // there is otherwise no way to see what is actually in there.
      static const bool sShotAlpha = ARGS.Get("shotalpha") != NULL;
      if (bmp.LockBits(&rc, Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB,
                       &bd) == Gdiplus::Ok) {
        for (int y = 0; y < h; y++) {
          const u8 *src = (const u8 *)ms.pData +
                          (size_t)y * ms.RowPitch; // backbuffer R8G8B8A8
          u8 *dst = (u8 *)bd.Scan0 + (size_t)y * bd.Stride; // GDI+ BGRA
          for (int x = 0; x < w; x++) {
            if (sShotAlpha) {
              dst[x * 4 + 0] = dst[x * 4 + 1] = dst[x * 4 + 2] = src[x * 4 + 3];
            } else {
              dst[x * 4 + 0] = src[x * 4 + 2]; // B
              dst[x * 4 + 1] = src[x * 4 + 1]; // G
              dst[x * 4 + 2] = src[x * 4 + 0]; // R
            }
            dst[x * 4 + 3] = 255;            // opaque
          }
        }
        bmp.UnlockBits(&bd);
        // Encode as whatever the caller named the file.  The bank's screenshot
        // button asks for .bmp, and writing PNG bytes into that name regardless
        // left a file nothing would open.
        const char *ext = strrchr(path, '.');
        const WCHAR *mime = L"image/png";
        if (ext) {
          if (!_stricmp(ext, ".bmp"))
            mime = L"image/bmp";
          else if (!_stricmp(ext, ".jpg") || !_stricmp(ext, ".jpeg"))
            mime = L"image/jpeg";
          else if (!_stricmp(ext, ".tif") || !_stricmp(ext, ".tiff"))
            mime = L"image/tiff";
        }
        CLSID enc;
        if (sGetEncoderClsid(mime, &enc) < 0 &&
            sGetEncoderClsid(L"image/png", &enc) < 0) {
          Errorf("gfxSaveScreenshot: no image encoder available");
        } else {
          WCHAR wpath[512];
          MultiByteToWideChar(CP_ACP, 0, path, -1, wpath, 512);
          if (bmp.Save(wpath, &enc, NULL) != Gdiplus::Ok)
            Errorf("gfxSaveScreenshot: could not write '%s'", path);
          else
            Displayf("screenshot saved to '%s'", path);
        }
      }
    }
    Gdiplus::GdiplusShutdown(tok);
    sCtx->Unmap(stg, 0);
  }
  stg->Release();
  back->Release();
}

bool gfxReadTexturePixels(gfxTexture *tex, std::vector<unsigned char> &rgba, int &width, int &height) {
  rgba.clear();
  width = height = 0;
  if (!tex || !tex->D3DTexture || !sDevice || !sCtx)
    return false;

  ID3D11Texture2D *srcTex = (ID3D11Texture2D *)tex->D3DTexture;
  D3D11_TEXTURE2D_DESC d;
  srcTex->GetDesc(&d);

  // Textures are created R8G8B8A8_UNORM, so the mapped rows are already the
  // RGBA byte order the .tex format wants.
  D3D11_TEXTURE2D_DESC sd = d;
  sd.Usage = D3D11_USAGE_STAGING;
  sd.BindFlags = 0;
  sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  sd.MiscFlags = 0;
  ID3D11Texture2D *stg = NULL;
  if (FAILED(sDevice->CreateTexture2D(&sd, NULL, &stg)) || !stg)
    return false;

  sCtx->CopyResource(stg, srcTex);
  D3D11_MAPPED_SUBRESOURCE ms;
  bool ok = false;
  if (SUCCEEDED(sCtx->Map(stg, 0, D3D11_MAP_READ, 0, &ms))) {
    width = (int)d.Width;
    height = (int)d.Height;
    rgba.resize((size_t)width * height * 4);
    for (int y = 0; y < height; y++) {
      const u8 *src = (const u8 *)ms.pData + (size_t)y * ms.RowPitch;
      memcpy(&rgba[(size_t)y * width * 4], src, (size_t)width * 4);
    }
    sCtx->Unmap(stg, 0);
    ok = true;
  }
  stg->Release();
  return ok;
}

void gfxSaveTexture(gfxTexture *tex, const char *path) {
  if (!tex || !tex->D3DTexture || !sDevice || !sCtx || !path)
    return;
  ID3D11Texture2D *srcTex = (ID3D11Texture2D *)tex->D3DTexture;
  D3D11_TEXTURE2D_DESC d;
  srcTex->GetDesc(&d);
  D3D11_TEXTURE2D_DESC sd = d;
  sd.Usage = D3D11_USAGE_STAGING;
  sd.BindFlags = 0;
  sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  sd.MiscFlags = 0;
  ID3D11Texture2D *stg = NULL;
  if (FAILED(sDevice->CreateTexture2D(&sd, NULL, &stg)) || !stg)
    return;
  sCtx->CopyResource(stg, srcTex);
  D3D11_MAPPED_SUBRESOURCE ms;
  if (SUCCEEDED(sCtx->Map(stg, 0, D3D11_MAP_READ, 0, &ms))) {
    Gdiplus::GdiplusStartupInput gi;
    ULONG_PTR tok;
    Gdiplus::GdiplusStartup(&tok, &gi, NULL);
    {
      int w = (int)d.Width, h = (int)d.Height;
      Gdiplus::Bitmap bmp(w, h, PixelFormat32bppARGB);
      Gdiplus::Rect rc(0, 0, w, h);
      Gdiplus::BitmapData bd;
      if (bmp.LockBits(&rc, Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &bd) == Gdiplus::Ok) {
        for (int y = 0; y < h; y++) {
          const u8 *src = (const u8 *)ms.pData + (size_t)y * ms.RowPitch;
          u8 *dst = (u8 *)bd.Scan0 + (size_t)y * bd.Stride;
          for (int x = 0; x < w; x++) {
            dst[x * 4 + 0] = src[x * 4 + 2];
            dst[x * 4 + 1] = src[x * 4 + 1];
            dst[x * 4 + 2] = src[x * 4 + 0];
            dst[x * 4 + 3] = src[x * 4 + 3];
          }
        }
        bmp.UnlockBits(&bd);
        CLSID png;
        if (sGetEncoderClsid(L"image/png", &png) >= 0) {
          WCHAR wpath[512];
          MultiByteToWideChar(CP_ACP, 0, path, -1, wpath, 512);
          bmp.Save(wpath, &png, NULL);
        }
      }
    }
    Gdiplus::GdiplusShutdown(tok);
    sCtx->Unmap(stg, 0);
  }
  stg->Release();
}

// ---------------------------------------------------------------------------
// Offscreen text targets: RenderStringIntoTexture bakes UI strings into an
// RGBA8 texture.  Between Begin/End the immediate-mode 2D path (Blit2D)
// renders into the target -- sWidth/sHeight are swapped so the ortho
gfxTexture *gfxCreateTextTarget(int w, int h) {
  if (!sDevice || w <= 0 || h <= 0)
    return NULL;
  D3D11_TEXTURE2D_DESC td = {};
  td.Width = (UINT)w;
  td.Height = (UINT)h;
  td.MipLevels = 1;
  td.ArraySize = 1;
  td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  td.SampleDesc.Count = 1;
  td.Usage = D3D11_USAGE_DEFAULT;
  td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
  ID3D11Texture2D *tex2d = NULL;
  if (FAILED(sDevice->CreateTexture2D(&td, NULL, &tex2d)) || !tex2d)
    return NULL;
  gfxTexture *tex = new gfxTexture();
  tex->SetName("textTarget");
  tex->Width = w;
  tex->Height = h;
  tex->D3DTexture = tex2d;
  tex->m_HasAlpha = true;
  sDevice->CreateShaderResourceView(tex2d, NULL, &tex->SRV);
  sDevice->CreateRenderTargetView(tex2d, NULL, &tex->RTV);
  if (!tex->SRV || !tex->RTV) {
    tex->Release();
    return NULL;
  }
  return tex;
}

// Depth buffers for render-to-texture.  Until now gfxBeginRenderToTexture bound
// a NULL depth-stencil view, so anything 3D drawn into a texture came out in
// draw order with no z -- which is why the city environment map and the
// reflected-objects composite had no PC path.  A caller that wants depth asks
// for it (gfxRTTDepth); the 2D users -- baked text, the gauge faces -- must not
// get one, because they draw overlapping quads at the same z.
//
// Targets come in whatever size the caller chose, so the buffers are cached by
// size.  Four is more than the game has ever had live at once; a fifth size
// recycles the last slot rather than silently render without z.
struct gfxRTTDepthBuf {
  int w, h;
  ID3D11Texture2D *tex;
  ID3D11DepthStencilView *dsv;
};
static gfxRTTDepthBuf sRTTDepths[4];

static ID3D11DepthStencilView *sGetRTTDepth(int w, int h) {
  if (!sDevice || w <= 0 || h <= 0)
    return NULL;
  for (int i = 0; i < 4; i++)
    if (sRTTDepths[i].dsv && sRTTDepths[i].w == w && sRTTDepths[i].h == h)
      return sRTTDepths[i].dsv;
  int slot = -1;
  for (int i = 0; i < 4; i++)
    if (!sRTTDepths[i].dsv) { slot = i; break; }
  if (slot < 0) {
    slot = 3;
    if (sRTTDepths[slot].dsv) sRTTDepths[slot].dsv->Release();
    if (sRTTDepths[slot].tex) sRTTDepths[slot].tex->Release();
    sRTTDepths[slot].dsv = NULL;
    sRTTDepths[slot].tex = NULL;
  }
  D3D11_TEXTURE2D_DESC dd = {};
  dd.Width = (UINT)w;
  dd.Height = (UINT)h;
  dd.MipLevels = 1;
  dd.ArraySize = 1;
  dd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
  dd.SampleDesc.Count = 1;
  dd.Usage = D3D11_USAGE_DEFAULT;
  dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
  ID3D11Texture2D *depth = NULL;
  if (FAILED(sDevice->CreateTexture2D(&dd, NULL, &depth)) || !depth)
    return NULL;
  ID3D11DepthStencilView *dsv = NULL;
  if (FAILED(sDevice->CreateDepthStencilView(depth, NULL, &dsv)) || !dsv) {
    depth->Release();
    return NULL;
  }
  sRTTDepths[slot].w = w;
  sRTTDepths[slot].h = h;
  sRTTDepths[slot].tex = depth;
  sRTTDepths[slot].dsv = dsv;
  Displayf("gfxBeginRenderToTexture: created a %dx%d depth buffer for render-to-texture (slot %d)", w, h, slot);
  return dsv;
}

void gfxBeginRenderToTexture(gfxTexture *tex) {
  gfxBeginRenderToTexture(tex, 0, 0x00000000u);
}

void gfxBeginRenderToTexture(gfxTexture *tex, int flags, unsigned clearColor) {
  if (!sCtx || !tex || !tex->RTV || sInRTT)
    return;
  sInRTT = true;
  sRTTSavedWidth = sWidth;
  sRTTSavedHeight = sHeight;
  sWidth = tex->GetWidth();
  sHeight = tex->GetHeight();
  sRTTBoundDSV = (flags & gfxRTTDepth) ? sGetRTTDepth(sWidth, sHeight) : NULL;
  sCtx->OMSetRenderTargets(1, &tex->RTV, sRTTBoundDSV);
  // gfxRTTNoClear skips BOTH clears.  A 3D pass clears its target itself (the
  // city environment map wants its own clear colour) and then binds the same
  // target several times in one frame - sky, then area lights, then glows - so
  // clearing on every bind would leave only whatever drew last.
  if (!(flags & gfxRTTNoClear)) {
    float clear[4];
    ColorToFloats(clearColor, clear[0], clear[1], clear[2], clear[3]);
    sCtx->ClearRenderTargetView(tex->RTV, clear);
    if (sRTTBoundDSV)
      sCtx->ClearDepthStencilView(sRTTBoundDSV, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
  }
  D3D11_VIEWPORT vp = {0.0f, 0.0f, (float)sWidth, (float)sHeight, 0.0f, 1.0f};
  sCtx->RSSetViewports(1, &vp);
}

// ---------------------------------------------------------------------------
// Environment probe: a small CPU-side copy of the live city environment map.
//
// The car shading in gfx/model.cpp is computed per VERTEX on the CPU, so it
// cannot sample a texture the way the chrome pass does; until now its
// "environment" was a hard-coded two-tone hemisphere, which is why cars looked
// the same in a neon-lit street and an unlit alley.  This keeps a 16x16
// average of the real map for it to read.
//
// Reading a render target back stalls the pipeline, so the copy is
// double-buffered: each update issues a CopyResource into one staging texture
// and maps the OTHER, which has had a frame to land, with DO_NOT_WAIT.  A frame
// where the map is not ready keeps the previous probe - at 16x16 and a couple
// of frames of latency, nothing about a reflection gives that away.
enum { kEnvProbeN = 16 };
static float sEnvProbe[kEnvProbeN * kEnvProbeN][3];
static bool sEnvProbeValid = false;
static ID3D11Texture2D *sEnvStage[2];
static bool sEnvStagePending[2];
static int sEnvStageSlot = 0;
static UINT sEnvStageW = 0, sEnvStageH = 0;
// The map itself, for the passes that CAN sample a texture (chrome reflects it
// through the reflection-vector texgen).  Not referenced: gfxEnvProbeInvalidate
// clears it when the city that owns it goes away.
static gfxTexture *sCityEnvMap = NULL;
// ...and the probe as a texture, which is what the reflections actually sample.
// The raw map is half bright sky and half black ground with a hard seam, and a
// reflection that crosses that seam inside one triangle comes out as a
// hard-edged grey patch - car bodywork went blotchy and faceted.  A reflection
// off car paint is a broad soft thing, so they sample the 16x16 average
// instead, which is smooth by construction.
static gfxTexture *sCityEnvProbeTex = NULL;

static void sEnvProbeFromPixels(const u8 *pix, UINT rowPitch, UINT w, UINT h) {
  if (!pix || w == 0 || h == 0) return;
  for (int cy = 0; cy < kEnvProbeN; cy++) {
    const UINT y0 = (UINT)((u64)cy * h / kEnvProbeN);
    UINT y1 = (UINT)((u64)(cy + 1) * h / kEnvProbeN);
    if (y1 <= y0) y1 = y0 + 1;
    for (int cx = 0; cx < kEnvProbeN; cx++) {
      const UINT x0 = (UINT)((u64)cx * w / kEnvProbeN);
      UINT x1 = (UINT)((u64)(cx + 1) * w / kEnvProbeN);
      if (x1 <= x0) x1 = x0 + 1;
      u32 sr = 0, sg = 0, sb = 0, n = 0;
      for (UINT y = y0; y < y1 && y < h; y++) {
        const u8 *row = pix + (size_t)y * rowPitch;
        for (UINT x = x0; x < x1 && x < w; x++) {
          sr += row[x * 4 + 0];
          sg += row[x * 4 + 1];
          sb += row[x * 4 + 2];
          n++;
        }
      }
      float *out = sEnvProbe[cy * kEnvProbeN + cx];
      if (n) {
        out[0] = (float)sr / (float)n / 255.0f;
        out[1] = (float)sg / (float)n / 255.0f;
        out[2] = (float)sb / (float)n / 255.0f;
      }
    }
  }
  sEnvProbeValid = true;

  // Publish it as a texture for the reflection passes.
  u8 rgba[kEnvProbeN * kEnvProbeN * 4];
  for (int i = 0; i < kEnvProbeN * kEnvProbeN; i++) {
    for (int c = 0; c < 3; c++) {
      float v = sEnvProbe[i][c];
      if (v < 0.0f) v = 0.0f; else if (v > 1.0f) v = 1.0f;
      rgba[i * 4 + c] = (u8)(v * 255.0f + 0.5f);
    }
    rgba[i * 4 + 3] = 255;
  }
  if (!sCityEnvProbeTex)
    sCityEnvProbeTex = gfxRegisterRgbaTexture("__cityenvprobe__", kEnvProbeN, kEnvProbeN, rgba);
  else
    gfxUpdateRgbaTexture(sCityEnvProbeTex, kEnvProbeN, kEnvProbeN, rgba);
}

extern "C" void gfxEnvProbeUpdate(gfxTexture *tex) {
  if (!sDevice || !sCtx || !tex || !tex->D3DTexture) return;
  sCityEnvMap = tex;
  ID3D11Texture2D *src = (ID3D11Texture2D *)tex->D3DTexture;
  D3D11_TEXTURE2D_DESC sd;
  src->GetDesc(&sd);
  if (sd.Format != DXGI_FORMAT_R8G8B8A8_UNORM) return;   // the downsample reads RGBA8
  if (sd.Width != sEnvStageW || sd.Height != sEnvStageH) {
    for (int i = 0; i < 2; i++) {
      if (sEnvStage[i]) { sEnvStage[i]->Release(); sEnvStage[i] = NULL; }
      sEnvStagePending[i] = false;
    }
    sEnvStageW = sd.Width;
    sEnvStageH = sd.Height;
  }
  for (int i = 0; i < 2; i++) {
    if (sEnvStage[i]) continue;
    D3D11_TEXTURE2D_DESC td = sd;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    td.MiscFlags = 0;
    if (FAILED(sDevice->CreateTexture2D(&td, NULL, &sEnvStage[i])) || !sEnvStage[i]) {
      sEnvStage[i] = NULL;
      return;
    }
  }
  const int other = sEnvStageSlot ^ 1;
  if (sEnvStagePending[other]) {
    D3D11_MAPPED_SUBRESOURCE ms;
    HRESULT hr = sCtx->Map(sEnvStage[other], 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &ms);
    if (hr == S_OK) {
      sEnvProbeFromPixels((const u8 *)ms.pData, ms.RowPitch, sEnvStageW, sEnvStageH);
      sCtx->Unmap(sEnvStage[other], 0);
      sEnvStagePending[other] = false;
    }
    // DXGI_ERROR_WAS_STILL_DRAWING: leave it pending and try again next frame.
  }
  sCtx->CopyResource(sEnvStage[sEnvStageSlot], src);
  sEnvStagePending[sEnvStageSlot] = true;
  sEnvStageSlot = other;
}

extern "C" bool gfxEnvProbeValid() { return sEnvProbeValid; }

extern "C" gfxTexture *gfxGetCityEnvMap() { return sCityEnvMap; }

extern "C" gfxTexture *gfxGetCityEnvProbeTexture() {
  return sEnvProbeValid ? sCityEnvProbeTex : NULL;
}

extern "C" void gfxEnvProbeInvalidate() {
  sEnvProbeValid = false;
  sCityEnvMap = NULL;
  // sCityEnvProbeTex is ours (a plain RGBA texture in the name cache), so it
  // outlives the city that fed it and is simply refilled next time.
  for (int i = 0; i < 2; i++) {
    if (sEnvStage[i]) { sEnvStage[i]->Release(); sEnvStage[i] = NULL; }
    sEnvStagePending[i] = false;
  }
  sEnvStageW = sEnvStageH = 0;
  sEnvStageSlot = 0;
}

extern "C" bool gfxEnvProbeSample(float u, float v, float *rgb) {
  if (!sEnvProbeValid || !rgb) return false;
  // Bilinear over the cell centres, clamped at the edges.
  float x = u * (float)kEnvProbeN - 0.5f;
  float y = v * (float)kEnvProbeN - 0.5f;
  if (x < 0.0f) x = 0.0f; else if (x > (float)(kEnvProbeN - 1)) x = (float)(kEnvProbeN - 1);
  if (y < 0.0f) y = 0.0f; else if (y > (float)(kEnvProbeN - 1)) y = (float)(kEnvProbeN - 1);
  const int x0 = (int)x, y0 = (int)y;
  const int x1 = x0 + 1 < kEnvProbeN ? x0 + 1 : x0;
  const int y1 = y0 + 1 < kEnvProbeN ? y0 + 1 : y0;
  const float fx = x - (float)x0, fy = y - (float)y0;
  for (int c = 0; c < 3; c++) {
    const float a = sEnvProbe[y0 * kEnvProbeN + x0][c] + (sEnvProbe[y0 * kEnvProbeN + x1][c] - sEnvProbe[y0 * kEnvProbeN + x0][c]) * fx;
    const float b = sEnvProbe[y1 * kEnvProbeN + x0][c] + (sEnvProbe[y1 * kEnvProbeN + x1][c] - sEnvProbe[y1 * kEnvProbeN + x0][c]) * fx;
    rgb[c] = a + (b - a) * fy;
  }
  return true;
}

void gfxEndRenderToTexture() {
  if (!sCtx || !sInRTT)
    return;
  sInRTT = false;
  sRTTBoundDSV = NULL;
  sWidth = sRTTSavedWidth;
  sHeight = sRTTSavedHeight;
  sCtx->OMSetRenderTargets(1, &sRTV, sDSV);
  D3D11_VIEWPORT vp = {0.0f, 0.0f, (float)sWidth, (float)sHeight, 0.0f, 1.0f};
  sCtx->RSSetViewports(1, &vp);
}

// -shot <file.png> [-shotdelay <sec>] [-shotframes <n>]: render for a moment
// (or exactly n presented frames -- deterministic, unlike wall-clock), save
// the backbuffer, then quit.  -quitafter <sec>: request exit after that much
// rendered time (unattended layout sweeps -- run_all.bat).  Both driven here
// (the universal present) so they work for every tester regardless of whether
// it uses the pipeManager convenience layer.  ageExit() polls gfxWantsExit()
// to leave the loop.
static bool sShotInited = false;
static bool sShotDone = false;
static bool sShotExit = false;
static const char *sShotPath = NULL;
static float sShotDelay = 2.0f;
static float sShotElapsed = 0.0f;
static int sShotFrames = 0;
static int sShotFrameCount = 0;
// -shotrace <sec>: capture at a fixed point on the RACE clock rather than after
// a number of rendered frames.  Rendered-frame counts drift between runs because
// mid-race streaming frames tick game time while drawing no 3D - they advance the
// race clock but not the frame count - so the same -shotframes value lands on a
// different race moment run to run.  Anchoring on the first car-shaded draw and
// then accumulating ticked game time (a constant 1/n under -fixedfps, streaming
// hitches included) puts the capture on the same race moment every time.
static float sShotRaceSecs = -1.0f;
static bool sShotRaceAnchored = false;
static float sShotRaceElapsed = 0.0f;
// -drawlog <frame>: print every draw of that presented frame (texture, topology,
// vertex count, screen-space extent) - a census of who paints the screen.
static int sDrawLogFrame = -1;
static bool sDrawLogAtShot = false;   // -drawlog shot: census the frame -shot captures
#include <string>
static std::string sFrameLog;         // this frame's census while -drawlog shot is armed
static int sPresentCount = 0;
static int sDrawLogIndex = 0;

// Clears go into the census between the draws they separate (a mid-frame depth
// clear is invisible in the draw lines but changes every depth-tested draw after it).
static void sDrawLogClear(unsigned flags, bool inRTT) {
  if (!((sDrawLogFrame >= 0 && sPresentCount == sDrawLogFrame) || sDrawLogAtShot))
    return;
  char line[128];
  snprintf(line, sizeof(line), "[drawlog] clear before #%03d color=%d z=%d stencil=%d rtt=%d\n",
           sDrawLogIndex, (flags & 1) ? 1 : 0, (flags & 2) ? 1 : 0, (flags & 4) ? 1 : 0, inRTT ? 1 : 0);
  if (sDrawLogAtShot) {
    sFrameLog += line;
  } else {
    fputs(line, stdout);
    fflush(stdout);
  }
}
static float sQuitAfterSecs = 0.0f;
static float sQuitAfterElapsed = 0.0f;
// -heapcheck: validate the heaps once per present to localize corruption.
static bool sHeapCheckEveryFrame = false;
extern "C" void ageHeapCheck(const char *tag);

// Deferred capture, queued by pipeManager::RequestScreenShot.  The request
// normally arrives from a bank widget callback, which runs at the top of a
// frame - long before anything has been drawn into the backbuffer - so reading
// the backbuffer there captures the wrong frame at best and, after a Present
// has discarded it, nothing at all.  gfxEndFrame takes the shot below, from the
// finished frame just before it is presented, exactly like -shot does.
static char sQueuedShotPath[512];
static bool sQueuedShot = false;

extern "C" void gfxQueueScreenshot(const char *path) {
  if (!path || !path[0])
    return;
  strncpy(sQueuedShotPath, path, sizeof(sQueuedShotPath) - 1);
  sQueuedShotPath[sizeof(sQueuedShotPath) - 1] = '\0';
  sQueuedShot = true;
}

extern "C" bool gfxScreenshotQueued() { return sQueuedShot; }

// On-demand capture (Pipe bank "Screenshot" button): saves the current frame
// to the next free screenshot_NNN.png in the working directory, no exit.
extern "C" void gfxScreenshotNow() {
  char path[64];
  for (int i = 0; i < 1000; i++) {
    snprintf(path, sizeof(path), "screenshot_%03d.png", i);
    FILE *f = fopen(path, "rb");
    if (!f)
      break;
    fclose(f);
  }
  printf("[screenshot] saving %s\n", path);
  fflush(stdout);
  gfxSaveScreenshot(path);
}

extern "C" bool gfxWantsExit() { return sShotExit; }

void pipeManager::SetCopyToFrontFunc(void (*func)()) { sCTFFunc = func; }

gfxTexture *gfxGetBackBufferCopy() {
  if (sBackCopy) return sBackCopy;
  if (!sDevice) return NULL;
  D3D11_TEXTURE2D_DESC td = {};
  td.Width = sWidth; td.Height = sHeight;
  td.MipLevels = 1; td.ArraySize = 1;
  td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  td.SampleDesc.Count = 1;
  td.Usage = D3D11_USAGE_DEFAULT;
  td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  ID3D11Texture2D *t = NULL;
  if (FAILED(sDevice->CreateTexture2D(&td, NULL, &t)) || !t) return NULL;
  sBackCopy = new gfxTexture();
  sBackCopy->Width = sWidth; sBackCopy->Height = sHeight;
  sBackCopy->D3DTexture = t;
  sBackCopyTex = t;
  sDevice->CreateShaderResourceView(t, NULL, &sBackCopy->SRV);
  sBackCopy->SetName("BackBufferCopy");
  return sBackCopy;
}

// Drop the backbuffer copy (pipeManager::DeleteRenderBuffer).  Holders that
// took a reference through CreateRenderTarget(rtargetBackFBMem) keep theirs;
// the next gfxGetBackBufferCopy allocates a fresh one.
void gfxReleaseBackBufferCopy() {
  if (!sBackCopy) return;
  gfxTexture *t = sBackCopy;
  sBackCopy = NULL;
  sBackCopyTex = NULL;
  t->Release();
}

bool gfxHasBackBufferCopy() { return sBackCopy != NULL; }

bool gfxRefreshBackBufferCopy() {
  if (!sCtx || !sSwap || !gfxGetBackBufferCopy() || !sBackCopyTex) return false;
  ID3D11Texture2D *back = NULL;
  if (FAILED(sSwap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&back)) || !back) return false;
  sCtx->CopyResource(sBackCopyTex, back);
  back->Release();
  return true;
}

bool gfxCopyTexture(gfxTexture *dst, const gfxTexture *src) {
  if (!sCtx || !dst || !src) return false;
  if (src == dst) return src == sBackCopy ? gfxRefreshBackBufferCopy() : true;
  if (src == sBackCopy) gfxRefreshBackBufferCopy();   // read the finished frame
  if (!dst->D3DTexture || !src->D3DTexture) return false;
  if (dst->Width == src->Width && dst->Height == src->Height) {
    sCtx->CopyResource(dst->D3DTexture, src->D3DTexture);
  } else {
    D3D11_BOX box = {0, 0, 0,
                     (UINT)(dst->Width < src->Width ? dst->Width : src->Width),
                     (UINT)(dst->Height < src->Height ? dst->Height : src->Height), 1};
    sCtx->CopySubresourceRegion(dst->D3DTexture, 0, 0, 0, 0, src->D3DTexture, 0, &box);
  }
  dst->m_HasAlpha = src->m_HasAlpha;
  dst->m_AllTranslucent = src->m_AllTranslucent;
  return true;
}

// Refresh the backbuffer copy and run the copy-to-front effect (fullscreen
// modulate/flash/blur from fxCopyToFront) on top of the finished frame.
static void RunCopyToFront() {
  if (!sCTFFunc || !sCtx) return;
  static int noCtf = -1;
  if (noCtf < 0) noCtf = (args::sm_Instance && ARGS.Get("noctf")) ? 1 : 0;  // -noctf: diagnostic
  if (noCtf) return;
  if (!gfxRefreshBackBufferCopy()) return;
  { static bool announced = false; if (!announced) { announced = true; Displayf("[rgl] copy-to-front hook active (%dx%d backbuffer copy)", sWidth, sHeight); } }
  sCTFActive = true;
  RSTATE.SetTexColorOp(0, texopModulate2X);   // PS2 blit colours: 0x80 = 1.0
  sCTFFunc();
  RSTATE.SetTexColorOp(0, texopModulate);
  RSTATE.SetAlphaBlendEnable(false);
  RSTATE.SetTexture(NULL);
  sCTFActive = false;
}

extern "C" void gfxSetShotSuspended(bool suspended) { sShotSuspended = suspended; }

// Called the first time a car-shaded material draws: that is the earliest frame
// the race scene is on screen, so it is where the -shotrace clock starts.
extern "C" void gfxNoteCarShadedDraw() { sShotRaceAnchored = true; }

static void FlushQueuedText();  // defined with the stroke font, below

//---------------------------------------------------------------------------
// -fpscap <n>: hold every frame to the same interval.
//
// Present here is DWM-composited in a window, which paces frames only loosely:
// measured intervals are a continuous 7-29 ms smear rather than multiples of a
// refresh, while the CPU work in a frame averages about 4 ms.  The simulation
// integrates the true frame delta, so it places the car correctly for the time
// the frame took to produce - but the frame is then displayed for a different
// length of time, and that mismatch is what reads as jitter.  Giving the frames
// a regular cadence removes the mismatch at its source.
//
// The schedule is absolute (target += interval) rather than "sleep whatever is
// left from now", so a late frame does not push every later frame late too.
//---------------------------------------------------------------------------
static double sFrameCapSeconds = 0.0;   // 0 = off
static double sFrameCapNext = 0.0;
static bool   sFrameCapInited = false;
static HANDLE sFrameCapTimer = NULL;    // high-resolution waitable timer, if we get one

static double rglNowSeconds() {
  LARGE_INTEGER c, f;
  QueryPerformanceCounter(&c);
  QueryPerformanceFrequency(&f);
  return f.QuadPart ? (double)c.QuadPart / (double)f.QuadPart : 0.0;
}

// Sleep until `seconds` from now, as precisely as the OS will allow.  A plain
// Sleep(1) can overshoot by a whole scheduler tick, which would put back all
// the jitter we are here to remove, so wait on a high-resolution timer and spin
// the last slice.
static void rglWaitSeconds(double seconds) {
  if (seconds <= 0.0)
    return;

  const double spin = 0.0012;           // hand the last ~1.2 ms to the spin loop
  if (seconds > spin && sFrameCapTimer) {
    LARGE_INTEGER due;
    due.QuadPart = -(LONGLONG)((seconds - spin) * 10000000.0);   // 100 ns units, relative
    if (SetWaitableTimer(sFrameCapTimer, &due, 0, NULL, NULL, FALSE))
      WaitForSingleObject(sFrameCapTimer, INFINITE);
  }
}

static void rglPaceFrame() {
  if (!sFrameCapInited) {
    sFrameCapInited = true;
    int fps = 0;
    if (ARGS.Get("fpscap", 0, fps) && fps > 0) {
      sFrameCapSeconds = 1.0 / (double)fps;
      // CREATE_WAITABLE_TIMER_HIGH_RESOLUTION needs Win10 1803; without it we
      // still pace, just with a longer spin.
      sFrameCapTimer = CreateWaitableTimerExW(NULL, NULL,
                                              CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                              TIMER_ALL_ACCESS);
      sFrameCapNext = rglNowSeconds() + sFrameCapSeconds;
      Displayf("[fpscap] pacing frames to %d fps (%.3f ms)%s",
               fps, sFrameCapSeconds * 1000.0,
               sFrameCapTimer ? "" : " - no high-resolution timer, spinning");
    }
  }

  if (sFrameCapSeconds <= 0.0)
    return;

  double now = rglNowSeconds();
  if (now < sFrameCapNext) {
    rglWaitSeconds(sFrameCapNext - now);
    while (rglNowSeconds() < sFrameCapNext) {
      YieldProcessor();
    }
  }

  sFrameCapNext += sFrameCapSeconds;

  // More than a whole frame behind: stop trying to pay off the debt, or the
  // pacer answers one slow frame with a burst of zero-length ones - which is
  // the very thing it exists to prevent.
  now = rglNowSeconds();
  if (sFrameCapNext < now)
    sFrameCapNext = now + sFrameCapSeconds;
}

// -novsync: present without waiting for the vertical blank.  A frame held to
// the refresh rate cannot be benchmarked - every measurement comes back as the
// refresh interval, whatever the renderer costs - and -fpscap is the pacing
// knob for ordinary play.
static UINT PresentSyncInterval() {
  static int sSync = -1;
  if (sSync < 0)
    sSync = (args::sm_Instance && ARGS.Get("novsync")) ? 0 : 1;
  return (UINT)sSync;
}

extern "C" void gfxEndFrame() {
  if (sInRTT) {
    gfxEndRenderToTexture();
  }
  // Before the loading-frame early-out below: those frames advance game time too,
  // and the race clock counts them, so the -shotrace clock has to as well.
  if (sShotRaceAnchored && timeManager::sm_Instance)
    sShotRaceElapsed += TIME.GetSeconds();
  RunCopyToFront();
  FlushQueuedText();  // stroke text lands AFTER the blur/modulate (PS2 parity)
  // A frame that submitted no 3D geometry is a loading screen or another
  // full-screen 2D blit, never gameplay: hold the -shot / -quitafter clocks
  // through it as well, which covers the loading paths that repaint by blitting
  // an image without going through gfxDrawLoadingBackdrop.
  if (sShotSuspended || sShotLoadingFrame || sSceneDrawsThisFrame == 0) {
    // Loading-screen frames: present them, but keep the -shot/-quitafter
    // clocks frozen so benchmarks still capture gameplay.
    if (!sShotLoadDone) {
      const char *lp = NULL;
      if (args::sm_Instance && ARGS.Get("shotload", 0, &lp) && lp) {
        sShotLoadDone = true;
        gfxSaveScreenshot(lp);
      }
    }
    gfxGpuTimerEndFrame();
    sSwap->Present(PresentSyncInterval(), 0);
    rglPaceFrame();
    ioMouse::ClearEdges();
    ioKeyboard::ClearEdges();
    sShotLoadingFrame = false;
    sSceneDrawsThisFrame = 0;
    return;
  }
  sPresentCount++;
  if (!sShotInited) {
    sShotInited = true;
    const char *dl = NULL;
    if (ARGS.Get("drawlog", 0, &dl) && dl) {
      if (!_stricmp(dl, "shot")) sDrawLogAtShot = true;
      else sDrawLogFrame = atoi(dl);
    }
    const char *p = NULL;
    if (ARGS.Get("shot", 0, &p) && p)
      sShotPath = p;
    const char *d = NULL;
    if (ARGS.Get("shotdelay", 0, &d) && d)
      sShotDelay = (float)atof(d);
    const char *fr = NULL;
    if (ARGS.Get("shotframes", 0, &fr) && fr)
      sShotFrames = atoi(fr);
    const char *rs = NULL;
    if (ARGS.Get("shotrace", 0, &rs) && rs)
      sShotRaceSecs = (float)atof(rs);
    const char *hc = NULL;
    sHeapCheckEveryFrame = ARGS.Get("heapcheck", 0, &hc);
    const char *q = NULL;
    if (ARGS.Get("quitafter", 0, &q) && q)
      sQuitAfterSecs = (float)atof(q);
  }
  if (sShotPath && !sShotDone) {
    bool due;
    if (sShotRaceSecs >= 0.0f) {
      // Only fires once the race scene has been seen; until then the clock is
      // not even running, so a slow load cannot shift the capture point.
      due = sShotRaceAnchored && sShotRaceElapsed >= sShotRaceSecs;
    } else if (sShotFrames > 0) {
      due = ++sShotFrameCount >= sShotFrames;
    } else {
      // Real seconds too, for the same reason as -quitafter below.
      static Timer sShotClock;
      static bool sShotClockStarted = false;
      if (!sShotClockStarted) {
        sShotClockStarted = true;
        sShotClock.Reset();
      }
      sShotElapsed = sShotClock.Time();
      due = sShotElapsed >= sShotDelay;
    }
    if (sDrawLogAtShot) {
      if (due) { fputs(sFrameLog.c_str(), stdout); fflush(stdout); }
      sFrameLog.clear();
      sDrawLogIndex = 0;
    }
    if (due) {
      // Same queue the bank buttons use; it fires below, before Present.
      gfxQueueScreenshot(sShotPath);
      sShotDone = true;
      sShotExit = true;
    }
  }
  if (sQuitAfterSecs > 0.0f && !sShotExit) {
    // Wall clock, not game time.  TIME.GetSeconds() is the frame delta CLAMPED
    // to timeManager's max (100 ms), and -fixedfps replaces it with a constant
    // outright - so summing it counts simulated seconds, not real ones.  A game
    // running at 4 fps advances that sum at under half real speed, and under
    // -fixedfps 60 at a fifteenth of it, which is why "-quitafter 25" could sit
    // there for six minutes.  A watchdog has to measure the thing it is
    // guarding against.
    static Timer sQuitAfterClock;
    static bool sQuitAfterStarted = false;
    if (!sQuitAfterStarted) {
      sQuitAfterStarted = true;
      sQuitAfterClock.Reset();
    }
    sQuitAfterElapsed = sQuitAfterClock.Time();
    if (sQuitAfterElapsed >= sQuitAfterSecs) {
      printf("[quitafter] %g seconds elapsed, requesting exit\n", sQuitAfterSecs);
      sShotExit = true;
    }
  }

  if (sHeapCheckEveryFrame)
    ageHeapCheck("frame");

  if (sQueuedShot) {
    sQueuedShot = false;
    gfxSaveScreenshot(sQueuedShotPath);   // the finished frame, before Present
  }

  gfxGpuTimerEndFrame();
  sSwap->Present(PresentSyncInterval(), 0);
  rglPaceFrame();

  // Universal per-frame boundary: every renderer presents through here (with
  // or without the pipeManager convenience layer).  WndProc feeds ioMouse/
  // ioKeyboard as messages arrive but nothing else resets the per-frame deltas
  // (GetDX/DY/DZ) or the pressed/released edges — clear them once per present
  // so standard per-frame input works for every tester.
  ioMouse::ClearEdges();
  ioKeyboard::ClearEdges();
  sShotLoadingFrame = false;
  sSceneDrawsThisFrame = 0;
}

extern "C" void gfxCloseDevice() {
  gfxGpuTimerReport("final");
  if (sCtx)
    sCtx->ClearState();
  // (leak-free release omitted for brevity during bootstrap)
}

////////////////////////////////////////////////////////////////////////////
// rgl* immediate mode

void rglWorldMatrix(const Matrix34 &m) { sWorld = ToXM(m); }
void rglColor3f(float r, float g, float b) { sCurColor = mkfrgb(r, g, b); }

EnumRenderMode ageRenderMode = renderSolid;
static D3D11_PRIMITIVE_TOPOLOGY sTopology = D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
static EnumDrawType sPrimType = drawLine;
static LineVertex sFanV0;
static LineVertex sFanVPrev;
static int sFanCount = 0;
static LineVertex sQuadV[4];
static int sQuadCount = 0;

void rglBegin(EnumDrawType prim, int vertexCount) {
  sVerts.Reset();
  sPrimType = prim;
  sFanCount = 0;
  sQuadCount = 0;
  if (prim == drawTriangles || prim == drawTri || prim == drawTriFan || prim == drawQuads) {
    sTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
  } else if (prim == drawTriStrip) {
    sTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
  } else if (prim == drawPoints) {
    sTopology = D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
  } else if (prim == drawLineStrip) {
    sTopology = D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;
  } else {
    sTopology = D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
  }
}

void rglVertex3f(float x, float y, float z) {
  float r, g, b, a;
  ColorToFloats(sCurColor, r, g, b, a);
  LineVertex v = {x, y, z, r, g, b, a, sCurU, sCurV, sCurU2, sCurV2, sCurNx, sCurNy, sCurNz};
  if (sPrimType == drawTriFan) {
    if (sFanCount == 0) {
      sFanV0 = v;
    } else if (sFanCount == 1) {
      sFanVPrev = v;
    } else {
      sVerts.Append(sFanV0);
      sVerts.Append(sFanVPrev);
      sVerts.Append(v);
      sFanVPrev = v;
    }
    sFanCount++;
  } else if (sPrimType == drawQuads) {
    sQuadV[sQuadCount++] = v;
    if (sQuadCount == 4) {
      sVerts.Append(sQuadV[0]);
      sVerts.Append(sQuadV[1]);
      sVerts.Append(sQuadV[2]);
      sVerts.Append(sQuadV[0]);
      sVerts.Append(sQuadV[2]);
      sVerts.Append(sQuadV[3]);
      sQuadCount = 0;
    }
  } else {
    sVerts.Append(v);
  }
}

static D3D11_TEXTURE_ADDRESS_MODE MapAddress(int a) {
  switch (a) {
  case texaddrClamp: return D3D11_TEXTURE_ADDRESS_CLAMP;
  case texaddrMirror: return D3D11_TEXTURE_ADDRESS_MIRROR;
  default: return D3D11_TEXTURE_ADDRESS_WRAP;
  }
}

// Sampler for a texture stage: the stage's address/filter selection, with the
// bound texture's clamp flags folded in.  States are built on first use.
static ID3D11SamplerState *GetSampler(int stage) {
  SamplerKey k = sSamplerKey[stage];
  const gfxTexture *tex = (stage == 0) ? sBoundTex : sBoundTex2;
  if (tex) {
    int env = tex->GetTexEnv();
    if (env & gfxTexEnvClampU) k.addrU = (u8)texaddrClamp;
    if (env & gfxTexEnvClampV) k.addrV = (u8)texaddrClamp;
  }
  for (int i = 0; i < sNumSamplers; i++) {
    const SamplerKey &c = sSamplerCache[i].key;
    if (c.addrU == k.addrU && c.addrV == k.addrV && c.linear == k.linear && c.mip == k.mip && c.bias == k.bias)
      return sSamplerCache[i].state;
  }
  if (sNumSamplers >= 32 || !sDevice) return sSampler;
  D3D11_SAMPLER_DESC sd = {};
  if (k.linear)
    sd.Filter = (k.mip == texfilterLinear) ? D3D11_FILTER_MIN_MAG_MIP_LINEAR : D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
  else
    sd.Filter = (k.mip == texfilterLinear) ? D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR : D3D11_FILTER_MIN_MAG_MIP_POINT;
  sd.AddressU = MapAddress(k.addrU);
  sd.AddressV = MapAddress(k.addrV);
  sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
  sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
  sd.MinLOD = 0;
  sd.MaxLOD = (k.mip == texfilterNone) ? 0.0f : D3D11_FLOAT32_MAX;
  sd.MipLODBias = k.bias;
  ID3D11SamplerState *st = NULL;
  if (FAILED(sDevice->CreateSamplerState(&sd, &st)) || !st) return sSampler;
  sSamplerCache[sNumSamplers].key = k;
  sSamplerCache[sNumSamplers].state = st;
  sNumSamplers++;
  return st;
}

// gfxBlendMode -> D3D11 factor.  The PS2 spellings (Cs/Cd/As/Ad/fixed) are
// the GS ALPHA register operands: source colour, dest colour, source alpha,
// dest alpha and the FIX constant (bound as the blend factor).
static D3D11_BLEND MapBlendFactor(gfxBlendFunc f) {
  switch (f) {
  case blendZero: return D3D11_BLEND_ZERO;
  case blendOne: return D3D11_BLEND_ONE;
  case blendSrcColor: case blendCs: return D3D11_BLEND_SRC_COLOR;
  case blendInvSrcColor: return D3D11_BLEND_INV_SRC_COLOR;
  case blendSrcAlpha: case blendAs: return D3D11_BLEND_SRC_ALPHA;
  case blendInvSrcAlpha: return D3D11_BLEND_INV_SRC_ALPHA;
  case blendDestAlpha: case blendAd: return D3D11_BLEND_DEST_ALPHA;
  case blendInvDestAlpha: return D3D11_BLEND_INV_DEST_ALPHA;
  case blendDestColor: case blendCd: return D3D11_BLEND_DEST_COLOR;
  case blendInvDestColor: return D3D11_BLEND_INV_DEST_COLOR;
  case blendSrcAlphaSat: return D3D11_BLEND_SRC_ALPHA_SAT;
  case blendFixed: return D3D11_BLEND_BLEND_FACTOR;
  default: return D3D11_BLEND_ONE;
  }
}

static D3D11_BLEND_OP MapBlendOp(int op) {
  switch (op) {
  case blendOpSubtract: return D3D11_BLEND_OP_SUBTRACT;
  case blendOpRevSubtract: return D3D11_BLEND_OP_REV_SUBTRACT;
  case blendOpMin: return D3D11_BLEND_OP_MIN;
  case blendOpMax: return D3D11_BLEND_OP_MAX;
  default: return D3D11_BLEND_OP_ADD;
  }
}

// The (src, dest, op) triple behind each EnumBlendSet preset.
static void BlendSetFactors(EnumBlendSet set, gfxBlendFunc &src, gfxBlendFunc &dst, int &op) {
  op = blendOpAdd;
  switch (set) {
  case blendSet_SrcAlpha_InvSrcAlpha: src = blendSrcAlpha; dst = blendInvSrcAlpha; break;
  case blendSet_One_One: src = blendOne; dst = blendOne; break;                          // additive
  case blendSet_MinusOne_One: src = blendOne; dst = blendOne; op = blendOpRevSubtract; break;   // dest - src
  case blendSet_One_SrcAlpha: src = blendOne; dst = blendSrcAlpha; break;
  case blendSet_SrcAlpha_One: src = blendSrcAlpha; dst = blendOne; break;                // lasers / glows
  case blendSet_InvSrcAlpha_SrcAlpha: src = blendInvSrcAlpha; dst = blendSrcAlpha; break;
  case blendSet_DestColor_Zero: src = blendDestColor; dst = blendZero; break;           // multiply (lightmaps)
  case blendSet_DestMinusSrc: src = blendOne; dst = blendOne; op = blendOpRevSubtract; break;
  case blendSet_InvSrcAlpha_Zero: src = blendInvSrcAlpha; dst = blendZero; break;
  case blendSet_SrcAlpha_Zero: src = blendSrcAlpha; dst = blendZero; break;
  case blendSet_DestAlpha_One: src = blendDestAlpha; dst = blendOne; break;             // rear-view mirror / checkpoint overlays
  case blendSet_DestAlpha_InvDestAlpha: src = blendDestAlpha; dst = blendInvDestAlpha; break;
  default: src = blendOne; dst = blendZero; break;                                       // blendSet_One_Zero (opaque replace)
  }
}

// Effective colour write mask for the next draw: SetColorMask, plus the
// alpha-fail approximation (afailZOnly = depth only while alpha testing).
static UINT8 EffectiveColorMask() {
  if (sAlphaFail == afailZOnly && sAlphaFunc != alphaAlways) return 0;
  return sColorMask;
}

// The alpha slots take no colour-based factor: D3D11 rejects SRC_COLOR / DEST_COLOR and their
// inverses in SrcBlendAlpha / DestBlendAlpha outright.  The PS2 blend sets draw no such
// distinction, so a multiply (blendSet_DestColor_Zero) handed DEST_COLOR to both slots,
// CreateBlendState failed, and the NULL state that came back read as "no blending" - the draw
// landed opaque while the state log still reported the set as live.  Fold each colour factor
// onto its matching alpha channel.
static D3D11_BLEND AlphaSlot(D3D11_BLEND b) {
  switch (b) {
  case D3D11_BLEND_SRC_COLOR: return D3D11_BLEND_SRC_ALPHA;
  case D3D11_BLEND_INV_SRC_COLOR: return D3D11_BLEND_INV_SRC_ALPHA;
  case D3D11_BLEND_DEST_COLOR: return D3D11_BLEND_DEST_ALPHA;
  case D3D11_BLEND_INV_DEST_COLOR: return D3D11_BLEND_INV_DEST_ALPHA;
  case D3D11_BLEND_SRC1_COLOR: return D3D11_BLEND_SRC1_ALPHA;
  case D3D11_BLEND_INV_SRC1_COLOR: return D3D11_BLEND_INV_SRC1_ALPHA;
  default: return b;
  }
}

static ID3D11BlendState *GetBlendState() {
  UINT8 mask = EffectiveColorMask();
  u32 key = sBlendEnable ? (1u << 31) | ((u32)sBlendSrc << 16) | ((u32)sBlendDst << 8) | ((u32)sBlendOp << 4) | mask
                         : (u32)mask;
  for (int i = 0; i < sNumBlendStates; i++)
    if (sBlendCache[i].key == key) return sBlendCache[i].state;

  D3D11_BLEND_DESC bd = {};
  D3D11_RENDER_TARGET_BLEND_DESC &rt = bd.RenderTarget[0];
  rt.RenderTargetWriteMask = mask;
  if (sBlendEnable) {
    rt.BlendEnable = TRUE;
    rt.SrcBlend = MapBlendFactor(sBlendSrc);
    rt.DestBlend = MapBlendFactor(sBlendDst);
    rt.BlendOp = MapBlendOp(sBlendOp);
    rt.SrcBlendAlpha = (sBlendSrc == blendSrcAlpha) ? D3D11_BLEND_ONE : AlphaSlot(MapBlendFactor(sBlendSrc));
    rt.DestBlendAlpha = (sBlendDst == blendInvSrcAlpha) ? D3D11_BLEND_INV_SRC_ALPHA : AlphaSlot(MapBlendFactor(sBlendDst));
    rt.BlendOpAlpha = MapBlendOp(sBlendOp);
  }
  ID3D11BlendState *st = NULL;
  if (FAILED(sDevice->CreateBlendState(&bd, &st)) || !st) {
    // A refused descriptor used to fail silently: OMSetBlendState(NULL) installs the default
    // state, so the draw blends not at all rather than wrongly.  Say so instead of guessing.
    Displayf("gfx: CreateBlendState refused src=%d dst=%d op=%d mask=%d; that draw will not blend",
             (int)sBlendSrc, (int)sBlendDst, sBlendOp, (int)mask);
  }
  if (sNumBlendStates < 64) {
    sBlendCache[sNumBlendStates].key = key;
    sBlendCache[sNumBlendStates].state = st;
    sNumBlendStates++;
  } else if (st) {
    // cache full: hand the state out uncached (released with the device)
  }
  return st;
}

static D3D11_COMPARISON_FUNC MapZFunc(EnumZFunc f) {
  switch (f) {
  case zNever:
    return D3D11_COMPARISON_NEVER;
  case zLess:
    return D3D11_COMPARISON_LESS;
  case zEqual:
    return D3D11_COMPARISON_EQUAL;
  case zLEqual:
    return D3D11_COMPARISON_LESS_EQUAL;
  case zGreater:
    return D3D11_COMPARISON_GREATER;
  case zNotEqual:
    return D3D11_COMPARISON_NOT_EQUAL;
  case zGEqual:
    return D3D11_COMPARISON_GREATER_EQUAL;
  case zAlways:
  default:
    return D3D11_COMPARISON_ALWAYS;
  }
}

static D3D11_STENCIL_OP MapStencilOp(int op) {
  switch (op) {
  case stencilopZero: return D3D11_STENCIL_OP_ZERO;
  case stencilopReplace: return D3D11_STENCIL_OP_REPLACE;
  case stencilopIncrSat: return D3D11_STENCIL_OP_INCR_SAT;
  case stencilopDecrSat: return D3D11_STENCIL_OP_DECR_SAT;
  case stencilopInvert: return D3D11_STENCIL_OP_INVERT;
  case stencilopIncr: return D3D11_STENCIL_OP_INCR;
  case stencilopDecr: return D3D11_STENCIL_OP_DECR;
  default: return D3D11_STENCIL_OP_KEEP;
  }
}

static bool SameStencil(const gfxStencilState &a, const gfxStencilState &b) {
  return a.enable == b.enable && a.func == b.func && a.readMask == b.readMask &&
         a.writeMask == b.writeMask && a.pass == b.pass && a.fail == b.fail && a.zfail == b.zfail;
}

static ID3D11DepthStencilState *GetDepthState() {
  bool raw2d = sRaw2D;
  bool background = sZBias <= -5000.0f;
  bool zwrite = !raw2d && sZWriteEnable && !((sAlphaFail == afailFBOnly || sAlphaFail == afailRGBOnly) && sAlphaFunc != alphaAlways);
  bool ztest = !raw2d && sZTestEnable && !background;
  int key =
      (ztest ? 1 : 0) | (zwrite ? 2 : 0) | ((int)sZFunc << 2);
  if (key < 0 || key >= 32)
    key = 0;
  if (background && !raw2d)
    key = 32;

  D3D11_DEPTH_STENCIL_DESC dd = {};
  bool test = ztest;
  bool write = zwrite;
  dd.DepthEnable = (test || write) ? TRUE : FALSE;
  dd.DepthWriteMask = write ? D3D11_DEPTH_WRITE_MASK_ALL
                            : D3D11_DEPTH_WRITE_MASK_ZERO;
  dd.DepthFunc = test ? MapZFunc(sZFunc) : D3D11_COMPARISON_ALWAYS;
  dd.StencilEnable = FALSE;

  if (sStencilMode == 3) {   // explicit stencil state (gfxRenderState::SetStencil)
    for (int i = 0; i < sNumCustomDepth; i++)
      if (sCustomDepth[i].depthKey == key && SameStencil(sCustomDepth[i].st, sStencil))
        return sCustomDepth[i].state;
    dd.StencilEnable = sStencil.enable ? TRUE : FALSE;
    dd.StencilReadMask = (UINT8)sStencil.readMask;
    dd.StencilWriteMask = (UINT8)sStencil.writeMask;
    dd.FrontFace.StencilFunc = MapZFunc(sStencil.func);
    dd.FrontFace.StencilPassOp = MapStencilOp(sStencil.pass);
    dd.FrontFace.StencilFailOp = MapStencilOp(sStencil.fail);
    dd.FrontFace.StencilDepthFailOp = MapStencilOp(sStencil.zfail);
    dd.BackFace = dd.FrontFace;
    ID3D11DepthStencilState *st = NULL;
    sDevice->CreateDepthStencilState(&dd, &st);
    if (sNumCustomDepth < 32) {
      sCustomDepth[sNumCustomDepth].depthKey = key;
      sCustomDepth[sNumCustomDepth].st = sStencil;
      sCustomDepth[sNumCustomDepth].state = st;
      sNumCustomDepth++;
    }
    return st;
  }

  key += sStencilMode * 33;
  if (!sDepthCache[key]) {
    if (sStencilMode == 1) {          // shadow volume: count front/back faces
      dd.DepthEnable = TRUE;
      dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
      dd.DepthFunc = D3D11_COMPARISON_LESS;
      dd.StencilEnable = TRUE;
      dd.StencilReadMask = 0xFF; dd.StencilWriteMask = 0xFF;
      dd.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
      dd.FrontFace.StencilPassOp = D3D11_STENCIL_OP_INCR;
      dd.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
      dd.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
      dd.BackFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
      dd.BackFace.StencilPassOp = D3D11_STENCIL_OP_DECR;
      dd.BackFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
      dd.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
    } else if (sStencilMode == 2) {   // apply where the count is non-zero
      dd.DepthEnable = FALSE;
      dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
      dd.StencilEnable = TRUE;
      dd.StencilReadMask = 0xFF; dd.StencilWriteMask = 0;
      dd.FrontFace.StencilFunc = D3D11_COMPARISON_NOT_EQUAL;
      dd.FrontFace.StencilPassOp = dd.FrontFace.StencilFailOp = dd.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
      dd.BackFace = dd.FrontFace;
    }
    sDevice->CreateDepthStencilState(&dd, &sDepthCache[key]);
  }
  return sDepthCache[key];
}

static int GetZBiasKey(float bias) {
  if (bias == 0.0f) return 0;
  if (bias <= -5000.0f) return 1;
  if (bias < 0.0f) return 2;
  return 3;
}

static ID3D11RasterizerState *sRasterCacheScissor[16] = {};

static ID3D11RasterizerState *GetRasterState() {
  int key = (int)sCullMode + GetZBiasKey(sZBias) * 3;
  if (key < 0 || key >= 16)
    key = 0;
  ID3D11RasterizerState **slot = sScissorEnable ? &sRasterCacheScissor[key] : &sRasterCache[key];
  if (!*slot) {
    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID; // wireframe is done via line primitives
    rd.CullMode = (sCullMode == cullCW)    ? D3D11_CULL_BACK
                  : (sCullMode == cullCCW) ? D3D11_CULL_FRONT
                                           : D3D11_CULL_NONE;
    // Unmirrored view (see SetCamera): screen winding matches world winding,
    // so front faces are CCW (AGE/OpenGL native).
    rd.FrontCounterClockwise = TRUE;
    rd.DepthClipEnable = TRUE;
    rd.DepthBias = (INT)sZBias;
    rd.ScissorEnable = sScissorEnable ? TRUE : FALSE;
    sDevice->CreateRasterizerState(&rd, slot);
  }
  return *slot;
}

// The current gfxViewport's window as a D3D11 viewport, clamped to the
// render target.  Every batch applies it, so PIPE.SetViewport / SetWindow
// take effect immediately (split-screen halves, HUD sub-windows, the menus'
// "simulate scissoring" window trick, envmap targets).
static void ApplyViewportWindow() {
  float x = 0.0f, y = 0.0f, w = (float)sWidth, h = (float)sHeight, zn = 0.0f, zf = 1.0f;
  const gfxViewport *vp = pipeManager::sm_Instance ? pipeManager::sm_Instance->GetViewport() : NULL;
  if (vp) {
    const gfxViewportParams &p = vp->GetViewportParams();
    x = p.m_X; y = p.m_Y; w = p.m_Width; h = p.m_Height;
    zn = p.m_MinZ; zf = p.m_MaxZ;
    if (zn < 0.0f || zn > 1.0f || zf <= zn || zf > 1.0f) { zn = 0.0f; zf = 1.0f; }
  }
  if (x < 0.0f) { w += x; x = 0.0f; }
  if (y < 0.0f) { h += y; y = 0.0f; }
  if (x + w > (float)sWidth) w = (float)sWidth - x;
  if (y + h > (float)sHeight) h = (float)sHeight - y;
  if (w <= 0.0f || h <= 0.0f) { x = 0.0f; y = 0.0f; w = (float)sWidth; h = (float)sHeight; }
  D3D11_VIEWPORT dvp = {x, y, w, h, zn, zf};
  sCtx->RSSetViewports(1, &dvp);
  if (sScissorEnable) {
    D3D11_RECT rc;
    rc.left = sScissorX; rc.top = sScissorY;
    rc.right = sScissorX + sScissorW; rc.bottom = sScissorY + sScissorH;
    if (rc.left < 0) rc.left = 0;
    if (rc.top < 0) rc.top = 0;
    if (rc.right > sWidth) rc.right = sWidth;
    if (rc.bottom > sHeight) rc.bottom = sHeight;
    if (rc.right < rc.left) rc.right = rc.left;
    if (rc.bottom < rc.top) rc.bottom = rc.top;
    sCtx->RSSetScissorRects(1, &rc);
  }
}

// 2D projection for screen-space draws: an ortho viewport supplies its own
// matrix (OrthoScreen = absolute pixels over its window, Ortho2D = the
// caller's range mapped into the window); while a perspective viewport is
// current, Blit2D-style draws map absolute screen pixels over that window.
static bool InOrthoViewport() {
  const gfxViewport *vp = pipeManager::sm_Instance ? pipeManager::sm_Instance->GetViewport() : NULL;
  return vp && vp->IsOrtho();
}

static bool InOrtho2DViewport() {
  const gfxViewport *vp = pipeManager::sm_Instance ? pipeManager::sm_Instance->GetViewport() : NULL;
  return vp && vp->GetProjMode() == gfxViewport::projOrtho;
}

static XMMATRIX OrthoProjection() {
  const gfxViewport *vp = pipeManager::sm_Instance ? pipeManager::sm_Instance->GetViewport() : NULL;
  // Raw 2D and vglOrtho draws address screen pixels; inside an Ortho2D
  // viewport they fall through to the pixel range over its window.
  bool screenPixels = (sRaw2D || sScreenOrtho) && InOrtho2DViewport();
  if (vp && vp->IsOrtho() && !screenPixels) {
    const Matrix44 &proj = vp->GetProjection();
    return XMMATRIX(proj.m[0], proj.m[1], proj.m[2], proj.m[3], proj.m[4], proj.m[5], proj.m[6], proj.m[7],
                    proj.m[8], proj.m[9], proj.m[10], proj.m[11], proj.m[12], proj.m[13], proj.m[14], proj.m[15]);
  }
  float x = 0.0f, y = 0.0f, w = (float)sWidth, h = (float)sHeight;
  if (vp) {
    const gfxViewportParams &p = vp->GetViewportParams();
    if (p.m_Width > 0.0f && p.m_Height > 0.0f) { x = p.m_X; y = p.m_Y; w = p.m_Width; h = p.m_Height; }
  }
  return XMMatrixOrthographicOffCenterLH(x, x + w, y + h, y, -1.0f, 1.0f);
}

// The projection the next batch draws with: the current viewport's matrix,
// or the bootstrap default when no pipeline exists.
static XMMATRIX CurrentProjection() {
  if (pipeManager::sm_Instance && pipeManager::sm_Instance->GetViewport()) {
    const Matrix44 &proj = pipeManager::sm_Instance->GetViewport()->GetProjection();
    return XMMATRIX(proj.m[0], proj.m[1], proj.m[2], proj.m[3], proj.m[4], proj.m[5], proj.m[6], proj.m[7],
                    proj.m[8], proj.m[9], proj.m[10], proj.m[11], proj.m[12], proj.m[13], proj.m[14], proj.m[15]);
  }
  return sProj;
}

// The full transform the next batch would use (what RSTATE.GetComposite hands
// the game): 2D paths use the ortho projection, everything else world * view
// * the viewport projection.
static XMMATRIX CurrentComposite() {
  if (sOrthoMode) {
    if (sRaw2D) return OrthoProjection();
    if (!sScreenOrtho && sOrthoVPCamera && InOrtho2DViewport()) return sWorld * sView * CurrentProjection();
    return sWorld * OrthoProjection();
  }
  return sWorld * sView * CurrentProjection();
}

const Matrix44 &gfxRenderState::GetComposite() const {
  XMFLOAT4X4 f;
  XMStoreFloat4x4(&f, CurrentComposite());
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 4; c++)
      m_Composite.m[r * 4 + c] = f.m[r][c];
  return m_Composite;
}

const Matrix34 &gfxRenderState::GetView() const {
  m_View.FastInverse(m_Camera);
  return m_View;
}

static void FlushLines() {
  if (sVerts.IsEmpty() || !sCtx) {
    sVerts.Reset();
    return;
  }
  int n = sVerts.GetCount();
  if (n > MAX_VERTS)
    n = MAX_VERTS;

  D3D11_MAPPED_SUBRESOURCE ms;
  sCtx->Map(sVB, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms);
  memcpy(ms.pData, sVerts.begin(), sizeof(LineVertex) * n);
  sCtx->Unmap(sVB, 0);

  // Raw 2D (Blit2D & co) ignores the world matrix like a PS2 sprite; a vgl
  // batch drawn into an ortho viewport keeps it (HUD needles rotate by it);
  // 3D batches use the current viewport's projection.
  XMMATRIX mvp = CurrentComposite();
  sCtx->Map(sCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms);
  memcpy(ms.pData, &mvp, sizeof(mvp));
  // gAlphaTest = (ref, func, enable, unused).  Enabled when func !=
  // alphaAlways.  With a colour-preserving alpha-fail mode the GS wrote the
  // failing fragment's colour anyway, so the shader must not discard it
  // (the depth/colour write masks carry the rest of the approximation).
  bool alphaFailPasses = (sAlphaFail == afailFBOnly || sAlphaFail == afailRGBOnly);
  float alphaTest[4] = {sAlphaRef, (float)sAlphaFunc,
                        (sAlphaFunc != alphaAlways && !alphaFailPasses) ? 1.0f : 0.0f, 0.0f};
  memcpy((char *)ms.pData + sizeof(mvp), alphaTest, sizeof(alphaTest));
  {
    XMMATRIX wv = sOrthoMode ? XMMatrixIdentity() : (sWorld * sView);
    float fogParams[4] = {sFogStart, sFogEnd, sFogMin, sFogMax};
    float fogColor[4] = {sFogColor[0], sFogColor[1], sFogColor[2], sFogPower};
    static int sNoFog = -1;
    if (sNoFog < 0) sNoFog = (args::sm_Instance && ARGS.Get("nofog")) ? 1 : 0;  // -nofog: diagnostic
    const bool orthoBatch = sOrthoMode || InOrthoViewport();   // HUD/2D: no fog, lighting or mod2x boost
    float fogMisc[4] = {(sFogEnable && sFogMode != fogNone && !orthoBatch && !sNoFog) ? 1.0f : 0.0f, (float)sFogMode, 0.0f, 0.0f};
    char *p = (char *)ms.pData + sizeof(mvp) + 16;
    memcpy(p, &wv, sizeof(wv));
    memcpy(p + sizeof(wv), fogParams, sizeof(fogParams));
    memcpy(p + sizeof(wv) + 16, fogColor, sizeof(fogColor));
    memcpy(p + sizeof(wv) + 32, fogMisc, sizeof(fogMisc));
    XMMATRIX world = sOrthoMode ? XMMatrixIdentity() : sWorld;
    static int sNoLight = -1;
    if (sNoLight < 0) sNoLight = (args::sm_Instance && ARGS.Get("nolight")) ? 1 : 0;  // -nolight: diagnostic
    float lightMisc[4] = {(sLitBatch && !sNoLight && RSTATE.GetLighting() && RSTATE.GetLightingMode() > 0 && !orthoBatch) ? 1.0f : 0.0f, 0, 0, 0};
    char *q = p + sizeof(wv) + 48;
    memcpy(q, &world, sizeof(world));
    memcpy(q + 64, lightMisc, 16);
    memcpy(q + 80, sLightAmbient, 16);
    memcpy(q + 96, sLightDir, 48);
    memcpy(q + 144, sLightCol, 48);
    // PS2 GS modulate treats vertex colour 0x80 as 1.0, so every textured
    // world draw effectively gets a 255/128 colour boost that the 1.0-max
    // UNORM path lacks (validated against the PS2 benchmark shots:
    // NomadBlocks tiles, M01_A01 ceiling).  Ortho/HUD draws stay 1:1.
    // -nomod2x: diagnostic, disable the boost.
    static int sNoMod2X = -1;
    if (sNoMod2X < 0) sNoMod2X = (args::sm_Instance && ARGS.Get("nomod2x")) ? 1 : 0;
    float texScale = sTexColorScale;
    if (!sNoMod2X && texScale == 1.0f && sTexEnable && sBoundTex && !orthoBatch)
      texScale = 255.0f / 128.0f;
    float texMisc[4] = {(float)sTexSrc[0], (float)sTexSrc[1],
                        (sTexEnable && sBoundTex2 && sBoundTex2->SRV) ? 1.0f + (float)sTex2ColorOp : 0.0f, texScale};
    memcpy(q + 192, texMisc, 16);
    memcpy(q + 208, &sTexMtx, 64);
    static int sNoTexMtx = -1;
    if (sNoTexMtx < 0) sNoTexMtx = (args::sm_Instance && ARGS.Get("notexmtx")) ? 1 : 0;  // -notexmtx: diagnostic
    // -alphamod2x: experimental, extend the GS 0x80 = 1.0 convention to ALPHA
    // (GS modulate is (At*Av)>>7, so mid alphas render ~2x stronger on PS2).
    static int sAlphaMod2X = -1;
    if (sAlphaMod2X < 0) sAlphaMod2X = (args::sm_Instance && ARGS.Get("alphamod2x")) ? 1 : 0;
    float alphaScale = (sAlphaMod2X && texScale > 1.0f) ? texScale : 0.0f;
    float texGen[4] = {sNoTexMtx ? 0.0f : (float)sTexGen[0], sNoTexMtx ? 0.0f : (float)sTexGen[1], alphaScale, (float)sTex2AlphaOp};
    memcpy(q + 272, texGen, 16);
    memcpy(q + 288, &sTexMtx2, 64);
  }
  sCtx->Unmap(sCB, 0);

  UINT stride = sizeof(LineVertex), off = 0;
  sCtx->IASetInputLayout(sLayout);
  sCtx->IASetVertexBuffers(0, 1, &sVB, &stride, &off);
  sCtx->IASetPrimitiveTopology(sTopology);
  sCtx->VSSetShader(sVS, NULL, 0);
  sCtx->VSSetConstantBuffers(0, 1, &sCB);
  sCtx->PSSetShader(sPS, NULL, 0);
  sCtx->PSSetConstantBuffers(0, 1, &sCB); // PS reads gAlphaTest

  ID3D11ShaderResourceView *srvs[2];
  srvs[0] = (sTexEnable && sBoundTex && sBoundTex->SRV) ? sBoundTex->SRV
                                                        : sDefaultWhiteSRV;
  // Stage 2 modulates; the white SRV makes it a no-op when nothing is bound.
  srvs[1] = (sTexEnable && sBoundTex2 && sBoundTex2->SRV) ? sBoundTex2->SRV
                                                          : sDefaultWhiteSRV;
  sCtx->PSSetShaderResources(0, 2, srvs);
  ID3D11SamplerState *samplers[2] = {GetSampler(0), GetSampler(1)};
  sCtx->PSSetSamplers(0, 2, samplers);

  // blendFixed (PS2 FIX operand) reads the texture factor as the constant.
  float blendFactor[4] = {((sTexFactor >> 16) & 255) / 255.0f, ((sTexFactor >> 8) & 255) / 255.0f,
                          (sTexFactor & 255) / 255.0f, ((sTexFactor >> 24) & 255) / 255.0f};

  bool origBlendEnable = sBlendEnable;
  EnumBlendSet origBlendSet = sBlendSet;
  gfxBlendFunc origBlendSrc = sBlendSrc, origBlendDst = sBlendDst;
  int origBlendOp = sBlendOp;
  bool origZWrite = sZWriteEnable;

  // Texture-alpha overrides, only when the caller has not chosen an opaque blend set
  // (sTexAlphaBlendAllowed): an all-translucent texture was forced to blend without depth,
  // which blended the city's sidewalks / grass / asphalt (alpha = wet-reflection mask) away.
  if (sTexAlphaBlendAllowed && sBoundTex && sBoundTex->IsAllTranslucent()) {
    sBlendEnable = true;
    if (sBlendSet == blendSet_One_Zero) {
      sBlendSet = blendSet_SrcAlpha_InvSrcAlpha;
      BlendSetFactors(sBlendSet, sBlendSrc, sBlendDst, sBlendOp);
    }
    sZWriteEnable = false;
  } else if (sTexAlphaBlendAllowed && !sZWriteEnable && sBoundTex && sBoundTex->HasAlpha() && (!sBlendEnable || sBlendSet == blendSet_One_Zero)) {
    sBlendEnable = true;
    sBlendSet = blendSet_SrcAlpha_InvSrcAlpha;
    BlendSetFactors(sBlendSet, sBlendSrc, sBlendDst, sBlendOp);
  }

  sCtx->OMSetBlendState(GetBlendState(), blendFactor, 0xffffffff);
  sCtx->OMSetDepthStencilState(GetDepthState(), (sStencilMode == 3) ? (UINT)sStencil.ref : 0);
  sCtx->RSSetState(GetRasterState());
  ApplyViewportWindow();

  if ((sDrawLogFrame >= 0 && sPresentCount == sDrawLogFrame) || sDrawLogAtShot) {
    // clip-space extent of the batch, for "what covers the screen"
    float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f, minz = 1e9f, maxz = -1e9f;
    int behind = 0;
    for (int i = 0; i < n; i++) {
      const LineVertex &lv = sVerts[i];
      XMVECTOR cp = XMVector4Transform(XMVectorSet(lv.x, lv.y, lv.z, 1.0f), mvp);
      float w = XMVectorGetW(cp);
      if (w <= 1e-6f) { behind++; continue; }
      float sx = XMVectorGetX(cp) / w, sy = XMVectorGetY(cp) / w, sz = XMVectorGetZ(cp) / w;
      if (sx < minx) minx = sx; if (sx > maxx) maxx = sx;
      if (sy < miny) miny = sy; if (sy > maxy) maxy = sy;
      if (sz < minz) minz = sz; if (sz > maxz) maxz = sz;
    }
    const LineVertex &v0 = sVerts[0];
    char line[512];
    const gfxViewport *logVP = pipeManager::sm_Instance ? pipeManager::sm_Instance->GetViewport() : NULL;
    const gfxViewportParams *logVPP = logVP ? &logVP->GetViewportParams() : NULL;
    snprintf(line, sizeof(line), "[drawlog] #%03d tex='%s'%s topo=%d n=%d ortho=%d raw=%d vp=%d(%.0f,%.0f %.0fx%.0f) blend=%d(%s) zw=%d zt=%d cull=%d sc=%d fog=%d lit=%d col=(%.2f %.2f %.2f %.2f) v0=(%.1f %.1f %.1f) clip x[%.2f %.2f] y[%.2f %.2f] z[%.2f %.2f] behind=%d\n",
           sDrawLogIndex++, (sBoundTex ? sBoundTex->GetName() : "-"), (sTexEnable ? "" : "(texoff)"), (int)sTopology, n, sOrthoMode ? 1 : 0, sRaw2D ? 1 : 0,
           logVP ? (int)logVP->GetProjMode() : -1, logVPP ? logVPP->m_X : 0.0f, logVPP ? logVPP->m_Y : 0.0f, logVPP ? logVPP->m_Width : 0.0f, logVPP ? logVPP->m_Height : 0.0f,
           (int)sBlendSet, sBlendEnable ? "on" : "off", sZWriteEnable ? 1 : 0, sZTestEnable ? 1 : 0, (int)sCullMode, sScissorEnable ? 1 : 0, sFogEnable ? 1 : 0, sLitBatch ? 1 : 0,
           v0.r, v0.g, v0.b, v0.a, v0.x, v0.y, v0.z, minx, maxx, miny, maxy, minz, maxz, behind);
    if (sDrawLogAtShot) {
      // -drawlog shot: keep this frame's census; gfxEndFrame prints it if the
      // frame turns out to be the captured one, else drops it.
      sFrameLog += line;
    } else {
      fputs(line, stdout);
      fflush(stdout);
    }
  }
  if (!sRaw2D && !sOrthoMode) sSceneDrawsThisFrame++;
  sCtx->Draw(n, 0);
  sLitBatch = false;

  sBlendEnable = origBlendEnable;
  sBlendSet = origBlendSet;
  sBlendSrc = origBlendSrc;
  sBlendDst = origBlendDst;
  sBlendOp = origBlendOp;
  sZWriteEnable = origZWrite;

  sVerts.Reset();
  sFanCount = 0;
  sQuadCount = 0;
}

void rglEnd() { FlushLines(); }

////////////////////////////////////////////////////////////////////////////
// vgl* — current-state immediate mode (bones / overlays).  Shares the line
// path; uses the current RSTATE world (SetIdentity/SetWorld) + camera.

static gfxPackedColor sForceColor = 0xffffffff;
static bool sForceColorOverride = false;

void vglBindTexture(gfxTexture *tex) { sBoundTex = tex; }
void tglColor4fImpl(float r, float g, float b, float a) { vglColor4f(r, g, b, a); }
// Binding stage 2 resets the combine to plain modulate (the .mod "blendset
// lightmap" case); the model draw and the D3D-compat layer set the real ops
// right after.  NoTexture (white) is an unbind: modulating by white is the
// identity, so nothing is lost and the shader skips the second sample.
void vglBindTexture2(gfxTexture *tex) {
  sBoundTex2 = (tex == NoTexture) ? NULL : tex;
  sTex2ColorOp = tex2colModulate;
  sTex2AlphaOp = tex2alphaModulate;
}
void vglTex2Combine(int mode) {
  if (mode == 1) { sTex2ColorOp = tex2colDecal; sTex2AlphaOp = tex2alphaCurrent; }
  else if (mode == 2) { sTex2ColorOp = tex2colAddByBaseAlpha; sTex2AlphaOp = tex2alphaCurrent; }
  else if (mode == 3) { sTex2ColorOp = tex2colDecalVertexColor; sTex2AlphaOp = tex2alphaCurrent; }
  else if (mode == 4) { sTex2ColorOp = tex2colAddByVertexAlpha; sTex2AlphaOp = tex2alphaCurrent; }
  else { sTex2ColorOp = tex2colModulate; sTex2AlphaOp = tex2alphaModulate; }
}
void vglTex2CombineOps(int colorOp, int alphaOp) {
  sTex2ColorOp = (colorOp >= tex2colModulate && colorOp <= tex2colDecalVertexColor) ? colorOp : tex2colModulate;
  sTex2AlphaOp = (alphaOp >= tex2alphaModulate && alphaOp <= tex2alphaTexture) ? alphaOp : tex2alphaModulate;
}
gfxTexture *vglGetTexture2() { return sBoundTex2; }
// Stage 1 of the AGE render state is the backend's second sampler.
void gfxRenderState::SetTexture(int stage, const gfxTexture *tex) {
  if (stage == 0) SetTexture(tex);
  else if (stage == 1) vglBindTexture2(const_cast<gfxTexture *>(tex));
}
const gfxTexture *gfxRenderState::GetTexture(int stage) const {
  return stage == 0 ? sBoundTex : stage == 1 ? sBoundTex2 : NULL;
}
void vglTexCoord2f(float u, float v) {
  sCurU = u;
  sCurV = v;
}
void vglTexCoord2f2(float u, float v) {
  sCurU2 = u;
  sCurV2 = v;
}
void vglColor(gfxPackedColor c) {
  if (sForceColorOverride) {
    sCurColor = sForceColor;
  } else {
    sCurColor = c;
  }
}
void vglBegin(EnumDrawType prim, int vertexCount) {
  rglBegin(prim, vertexCount);
}
void vglVertex3f(const Vector3 &v) { rglVertex3f(v.x, v.y, v.z); }
void vglVertex3f(float x, float y, float z) { rglVertex3f(x, y, z); }
void vglEnd() { FlushLines(); }

// Camera-facing quad: spans the camera's right/up axes so it always faces
// the viewer, in the current world space (rain splashes, sparks).
void vglDrawParticle(const Vector3 &pos, float size, const Vector4 &color) {
  const Matrix34 &cam = RSTATE.GetCamera();
  Vector3 right(cam.a), up(cam.b);
  right.Scale(size * 0.5f);
  up.Scale(size * 0.5f);
  Vector3 p0, p1, p2, p3;
  p0.Subtract(pos, right); p0.Add(up);        // top-left
  p1.Add(pos, right);      p1.Add(up);        // top-right
  p2.Subtract(pos, right); p2.Subtract(up);   // bottom-left
  p3.Add(pos, right);      p3.Subtract(up);   // bottom-right
  vglColor(mkfrgba(color.x, color.y, color.z, color.w));
  vglBegin(drawTriStrip, 4);
  vglTexCoord2f(0.0f, 0.0f); vglVertex3f(p0);
  vglTexCoord2f(1.0f, 0.0f); vglVertex3f(p1);
  vglTexCoord2f(0.0f, 1.0f); vglVertex3f(p2);
  vglTexCoord2f(1.0f, 1.0f); vglVertex3f(p3);
  vglEnd();
}

// PS2 fog sprite: lerp the frame toward `color` by the destination alpha the
// fogged passes left behind (colour * DA + frame * (1 - DA)), over the
// current viewport window.  Depth is neither tested nor written.
void gfxFogSprite(gfxPackedColor color) {
  if (!sCtx) return;
  bool blend = sBlendEnable; gfxBlendFunc src = sBlendSrc, dst = sBlendDst; int op = sBlendOp;
  bool zt = sZTestEnable, zw = sZWriteEnable;
  gfxTexture *tex = sBoundTex;
  sBlendEnable = true; sBlendSrc = blendDestAlpha; sBlendDst = blendInvDestAlpha; sBlendOp = blendOpAdd;
  sZTestEnable = false; sZWriteEnable = false;
  sBoundTex = NULL;
  float x = 0.0f, y = 0.0f, w = (float)sWidth, h = (float)sHeight;
  const gfxViewport *vp = pipeManager::sm_Instance ? pipeManager::sm_Instance->GetViewport() : NULL;
  if (vp) {
    const gfxViewportParams &p = vp->GetViewportParams();
    if (p.m_Width > 0.0f && p.m_Height > 0.0f) { x = p.m_X; y = p.m_Y; w = p.m_Width; h = p.m_Height; }
  }
  pipeManager::sm_Instance->Blit2D(x, y, x + w, y + h, 0.0f, 0.0f, 1.0f, 1.0f, color);
  sBoundTex = tex;
  sBlendEnable = blend; sBlendSrc = src; sBlendDst = dst; sBlendOp = op;
  sZTestEnable = zt; sZWriteEnable = zw;
}

////////////////////////////////////////////////////////////////////////////
// Minimal render state (enough for the grid pass)

bool g_Allow8BitImages = true;
gfxRenderState RSTATE;

void gfxRenderState::SetCamera(const Matrix34 &m) {
  m_Camera = m;
  // The camera matrix's forward column c is the boom (from the look target back
  // to the eye) -- AGE's right-handed convention where the camera looks down -c
  // (see Matrix34::LookAt). Negate c ALONE: that maps AGE camera space onto the
  // D3D LH view basis (a = screen-right, b = up, -c = forward-into-screen). The
  // det goes -1 deliberately -- that IS the one-time RH->LH handedness
  // conversion at the D3D boundary. Negating a as well (to "keep det +1") yaws
  // the camera 180 degrees and MIRRORS screen X: world +x rendered on the left
  // of screen, so pressing stick-right visibly ran the character left even
  // though the world-space math was correct. Triangle winding flips with the
  // mirror; see FrontCounterClockwise at the two rasterizer sites.
  // Rigid inverse (transpose + back-rotated translation), as AGE's camera
  // matrices are orthonormal.  A full inverse NaNs on the HUD map's camera,
  // which flattens z with a zero scale before it is inverted.
  Matrix34 view = m;
  view.c = -m.c;
  Matrix34 inv;
  inv.FastInverse(view);
  sView = ToXM(inv);
  sOrthoVPCamera = true;
}
void gfxRenderState::SetCamera(const Matrix44 &m) {
  Matrix34 m34;
  m34.a.Set(m.a.x, m.a.y, m.a.z);
  m34.b.Set(m.b.x, m.b.y, m.b.z);
  m34.c.Set(m.c.x, m.c.y, m.c.z);
  m34.d.Set(m.d.x, m.d.y, m.d.z);
  SetCamera(m34);
}
void gfxRenderState::SetWorld(const Matrix34 &m) { m_World = m; sWorld = ToXM(m); }
void gfxRenderState::SetWorld(const Matrix44 &m) {
  // Full 4x4 pass-through (row-major, matching ToXM's Matrix34 layout).
  sWorld = XMMATRIX(m.a.x, m.a.y, m.a.z, m.a.w,
                    m.b.x, m.b.y, m.b.z, m.b.w,
                    m.c.x, m.c.y, m.c.z, m.c.w,
                    m.d.x, m.d.y, m.d.z, m.d.w);
}
void gfxRenderState::SetIdentity() { sWorld = XMMatrixIdentity(); }
void gfxRenderState::SetLighting(bool on) { m_LightingEnabled = on; }
void gfxRenderState::SetLightingMode(int mode) {
  m_LightingMode = mode;
  m_LightingEnabled = (mode > 0);
}
// Explicit lights (the rmcLightGroup path in SetLights fills the same slots).
// gLightDir.w: 0 off, 1 directional (xyz = travel direction), 2 point (xyz =
// position, gLightCol.w = intensity for the 1/d^2 falloff).
static float sLightSaved[3][4] = {{0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}};
static bool sLightOn[3] = {true, true, true};
void gfxRenderState::SetLight(int index, const gfxLight *light) {
  if (index < 0 || index > 2) return;
  if (!light) {
    sLightSaved[index][3] = 0.0f;
    sLightDir[index][3] = 0.0f;
    return;
  }
  float r = light->Color.r, g = light->Color.g, b = light->Color.b;
  if (r == 0.0f && g == 0.0f && b == 0.0f) { r = light->diffuse.x; g = light->diffuse.y; b = light->diffuse.z; }
  if (light->type == lightDirectional) {
    sLightSaved[index][0] = light->dir.x; sLightSaved[index][1] = light->dir.y; sLightSaved[index][2] = light->dir.z;
    sLightSaved[index][3] = 1.0f;
    sLightCol[index][3] = 0.0f;
  } else {
    sLightSaved[index][0] = light->dir.x; sLightSaved[index][1] = light->dir.y; sLightSaved[index][2] = light->dir.z;   // position for point lights
    sLightSaved[index][3] = 2.0f;
    float range = light->range > 0.0f ? light->range : 1000.0f;
    sLightCol[index][3] = range * range * 0.25f;     // full intensity out to half the range
  }
  sLightCol[index][0] = r; sLightCol[index][1] = g; sLightCol[index][2] = b;
  memcpy(sLightDir[index], sLightSaved[index], 16);
  if (!sLightOn[index]) sLightDir[index][3] = 0.0f;
}
void gfxRenderState::LightEnable(int index, bool enable) {
  if (index < 0 || index > 2) return;
  sLightOn[index] = enable;
  sLightDir[index][3] = enable ? sLightSaved[index][3] : 0.0f;
}
bool gfxRenderState::GetLightEnable(int index) const {
  return index >= 0 && index <= 2 && sLightOn[index] && sLightDir[index][3] != 0.0f;
}
bool gfxRenderState::GetDirectionalLight(int index, Vector3 &towardsLight, Vector3 &color) const {
  // the live slot (SetLight and the rmcLightGroup path both fill it): 1 = directional, 3 = fx directional
  if (index < 0 || index > 2 || (sLightDir[index][3] != 1.0f && sLightDir[index][3] != 3.0f)) return false;
  towardsLight.Set(-sLightDir[index][0], -sLightDir[index][1], -sLightDir[index][2]);
  if (towardsLight.MagSq() < 1e-8f) return false;
  towardsLight.Normalize();
  color.Set(sLightCol[index][0], sLightCol[index][1], sLightCol[index][2]);
  return true;
}
void gfxRenderState::DisableAllLights() {
  for (int i = 0; i < 3; i++) { sLightOn[i] = false; sLightDir[i][3] = 0.0f; }
}
void gfxRenderState::GetLightSlot(int index, int &mode, Vector3 &dirOrPos, Vector3 &color) const {
  mode = 0; dirOrPos.Set(0.0f); color.Set(0.0f);
  if (index < 0 || index > 2) return;
  mode = (int)sLightDir[index][3];
  dirOrPos.Set(sLightDir[index][0], sLightDir[index][1], sLightDir[index][2]);
  color.Set(sLightCol[index][0], sLightCol[index][1], sLightCol[index][2]);
}
void gfxRenderState::GetAmbientColor(Vector3 &color) const {
  color.Set(sLightAmbient[0], sLightAmbient[1], sLightAmbient[2]);
}
void gfxRenderState::SetAmbient(gfxColor c) {
  sLightAmbient[0] = c.r; sLightAmbient[1] = c.g; sLightAmbient[2] = c.b;
}
void gfxRenderState::SetAmbient(gfxPackedColor c) {
  sLightAmbient[0] = ((c >> 16) & 255) / 255.0f; sLightAmbient[1] = ((c >> 8) & 255) / 255.0f; sLightAmbient[2] = (c & 255) / 255.0f;
}
void gfxRenderState::SetBaseColor(const gfxColor &c) { SetBaseColor(c.r, c.g, c.b, c.a); }
void gfxRenderState::SetBaseColor(float r, float g, float b, float a) {
  bool save = sForceColorOverride;
  sForceColorOverride = false;
  vglColor(mkfrgba(r, g, b, a));
  sForceColorOverride = save;
}
void gfxRenderState::SetForceColor(const gfxColor &c) { SetForceColor((unsigned long)mkfrgba(c.r, c.g, c.b, c.a)); }
void gfxRenderState::SetTextureMatrix(int stage, const Matrix34 &m) {
  if (&m == 0) {                       // game passes NULL to reset the stage
    if (stage == 0) sTexMtx = XMMatrixIdentity();
    return;
  }
  Matrix44 m44;
  m44.a.x = m.a.x; m44.a.y = m.a.y; m44.a.z = m.a.z; m44.a.w = 0.0f;
  m44.b.x = m.b.x; m44.b.y = m.b.y; m44.b.z = m.b.z; m44.b.w = 0.0f;
  m44.c.x = m.c.x; m44.c.y = m.c.y; m44.c.z = m.c.z; m44.c.w = 0.0f;
  m44.d.x = m.d.x; m44.d.y = m.d.y; m44.d.z = m.d.z; m44.d.w = 1.0f;
  SetTexMatrix(stage, m44);
}
float gfxRenderState::GetZBias() const { return sZBias; }
gfxAlphaFunc gfxRenderState::GetAlphaFunc() const { return (gfxAlphaFunc)sAlphaFunc; }
int gfxRenderState::GetAlphaRef() const { return (int)(sAlphaRef * 255.0f + 0.5f); }
void gfxRenderState::SetSrcBlend(gfxBlendFunc func) { sBlendSrc = func; }
void gfxRenderState::SetDestBlend(gfxBlendFunc func) { sBlendDst = func; }
gfxBlendFunc gfxRenderState::GetSrcBlend() const { return sBlendSrc; }
gfxBlendFunc gfxRenderState::GetDestBlend() const { return sBlendDst; }
void gfxRenderState::SetBlendOp(int op) { sBlendOp = op; }
int gfxRenderState::GetBlendOp() const { return sBlendOp; }
void gfxRenderState::SetAlphaFail(int mode) { m_AlphaFail = mode; sAlphaFail = mode; }
void gfxRenderState::SetScissor(int x, int y, int w, int h) {
  if (w <= 0 || h <= 0) { sScissorEnable = false; return; }
  sScissorEnable = true;
  sScissorX = x; sScissorY = y; sScissorW = w; sScissorH = h;
}
void gfxRenderState::ClearScissor() { sScissorEnable = false; }
bool gfxRenderState::GetScissor(int &x, int &y, int &w, int &h) const {
  x = sScissorX; y = sScissorY; w = sScissorW; h = sScissorH;
  return sScissorEnable;
}
void vglSetFormat(int format) { sVglFormat = format; }
int vglGetFormat() { return sVglFormat; }
bool rglIsEnabled(EnumRglCap cap) {
  if (cap == RGL_LIGHTING) return RSTATE.GetLighting();
  if (cap == RGL_TEXTURE_2D) return sTexEnable;
  return false;
}
void rglEnableDisable(EnumRglCap cap, bool enable) {
  if (cap == RGL_LIGHTING) RSTATE.SetLighting(enable);
  else if (cap == RGL_TEXTURE_2D) RSTATE.SetTexEnable(enable);
}
static gfxMaterial sBoundMaterial;
void gfxRenderState::SetMaterial(const gfxMaterial *mat) {
  m_ActiveMaterial = mat;
  if (mat) {
    sBoundMaterial = *mat;
  }
}
void gfxRenderState::SetForceColor(unsigned long color) {
  sForceColor = color;
  sForceColorOverride = (color != 0xffffffff);
}
void gfxRenderState::SetTexture(const gfxTexture *tex) {
  sBoundTex = const_cast<gfxTexture*>(tex);
}
const gfxTexture *gfxRenderState::GetTexture() const { return sBoundTex; }
void gfxRenderState::SetFillMode(EnumFillMode mode) {
  if (mode == fillWire) {
    ageRenderMode = renderWireframe;
  } else {
    ageRenderMode = renderSolid;
  }
}
void gfxRenderState::SetAlphaBlendEnable(bool on) { sBlendEnable = on; }
bool gfxRenderState::GetAlphaBlendEnable() const { return sBlendEnable; }
void gfxRenderState::SetTextureAlphaBlendAllowed(bool allow) { sTexAlphaBlendAllowed = allow; }
bool gfxRenderState::GetTextureAlphaBlendAllowed() const { return sTexAlphaBlendAllowed; }
void gfxRenderState::SetBlendSet(EnumBlendSet set) {
  sBlendSet = set;
  BlendSetFactors(set, sBlendSrc, sBlendDst, sBlendOp);
  sBlendEnable = (set != blendSet_One_Zero);
}
EnumBlendSet gfxRenderState::GetBlendSet() const { return sBlendSet; }

// Depth / cull / fog state — applied to every immediate-mode batch in
// FlushLines.
void gfxRenderState::SetZFunc(EnumZFunc func) { sZFunc = func; }
EnumZFunc gfxRenderState::GetZFunc() const { return sZFunc; }
void gfxRenderState::SetZTestEnable(bool on) { sZTestEnable = on; }
void gfxRenderState::SetZWriteEnable(bool on) { sZWriteEnable = on; }
void gfxRenderState::SetZBias(float bias) { sZBias = bias; }
void gfxRenderState::SetCull(gfxCullMode mode) { sCullMode = mode; }
gfxCullMode gfxRenderState::GetCull() const { return sCullMode; }
void gfxRenderState::SetFogEnable(bool on) { sFogEnable = on; }
void gfxRenderState::SetFogParams(unsigned long color, float start, float end, float min, float max) {
  sFogColor[0] = ((color >> 16) & 0xFF) / 255.0f;
  sFogColor[1] = ((color >> 8) & 0xFF) / 255.0f;
  sFogColor[2] = (color & 0xFF) / 255.0f;
  sFogStart = start; sFogEnd = end; sFogMin = min; sFogMax = max;
}
void gfxRenderState::SetFogPower(float power) { sFogPower = power; }
void gfxRenderState::SetFogMode(int mode) { sFogMode = mode; }
int gfxRenderState::GetFogMode() const { return sFogMode; }
void gfxRenderState::SetFogStart(float start) { sFogStart = start; }
void gfxRenderState::SetFogEnd(float end) { sFogEnd = end; }
void gfxRenderState::SetFogColor(unsigned long color) {
  sFogColor[0] = ((color >> 16) & 0xFF) / 255.0f;
  sFogColor[1] = ((color >> 8) & 0xFF) / 255.0f;
  sFogColor[2] = (color & 0xFF) / 255.0f;
}
void gfxRenderState::SetLights(const rmcLightGroup &lights) {
  sLightAmbient[0] = lights.Ambient.x; sLightAmbient[1] = lights.Ambient.y; sLightAmbient[2] = lights.Ambient.z;
  for (int i = 0; i < 3; i++) {
    const Vector3 &d = lights.Dir[i];
    const Vector3 &p = lights.Pos[i];
    float inten = lights.Intensity[i];
    float mode = 0.0f, atten = 0.0f;
    if (lights.Mode[i] == 1) {                       // plain directional
      if (d.x * d.x + d.y * d.y + d.z * d.z > 1e-6f) mode = 1.0f;
      sLightDir[i][0] = d.x; sLightDir[i][1] = d.y; sLightDir[i][2] = d.z;
    } else if (inten >= 1e30f) {                     // fx directional emulated as a far point (lgtLight::SetLightData)
      mode = 3.0f;
      sLightDir[i][0] = d.x; sLightDir[i][1] = d.y; sLightDir[i][2] = d.z;
    } else if (inten > 0.0f) {                       // fx point light: Intensity / dist^2 falloff
      mode = 2.0f; atten = inten;
      sLightDir[i][0] = p.x; sLightDir[i][1] = p.y; sLightDir[i][2] = p.z;
    }
    sLightDir[i][3] = mode;
    sLightCol[i][0] = lights.Color[i].x; sLightCol[i][1] = lights.Color[i].y; sLightCol[i][2] = lights.Color[i].z; sLightCol[i][3] = atten;
  }
}
void vglNormal3f(float x, float y, float z) { sCurNx = x; sCurNy = y; sCurNz = z; sLitBatch = true; }
void gfxRenderState::SetTexGeneration(int stage, int mode, bool, int src, int) {
  if (stage < 0 || stage > 1) return;
  sTexSrc[stage] = (mode == 0 && src >= 1 && src <= 7) ? 1 : 0;   // only UV sets 0/1 exist in the vertex stream
  if (mode == 2) sTexGen[stage] = 1;                                // UV * texture matrix
  else if (src == texsrcCameraSpaceNormal) sTexGen[stage] = 2;     // sphere map from the view-space normal
  else if (src == texsrcCameraSpaceReflectionVector) sTexGen[stage] = 3;   // ... from the reflection vector
  else sTexGen[stage] = 0;
  // Generated coordinates go into that stage's OWN varying (the VS writes stage 0's
  // into o.tex and stage 1's into o.tex2), so the UV-set selector has to point there.
  // Without this, stage 1's sphere map was computed and then never read: the pixel
  // shader sampled it with the base UVs instead (city_window_daytime's reflection
  // layer, and now car paint).
  if (sTexGen[stage] != 0) sTexSrc[stage] = stage;
}
bool gfxRenderState::GetZTestEnable() const { return sZTestEnable; }
bool gfxRenderState::GetZWriteEnable() const { return sZWriteEnable; }
bool gfxRenderState::GetFogEnable() const { return sFogEnable; }

// Colour write mask -> blend-state write mask (rebuild the cache on change).
void gfxRenderState::SetColorMask(bool rgb, bool alpha) {
  UINT8 m = (UINT8)((rgb ? (D3D11_COLOR_WRITE_ENABLE_RED |
                            D3D11_COLOR_WRITE_ENABLE_GREEN |
                            D3D11_COLOR_WRITE_ENABLE_BLUE)
                         : 0) |
                    (alpha ? D3D11_COLOR_WRITE_ENABLE_ALPHA : 0));
  sColorMask = m;
}
void gfxRenderState::SetColorMask(bool r, bool g, bool b, bool a) {
  UINT8 m = (UINT8)((r ? D3D11_COLOR_WRITE_ENABLE_RED : 0) |
                    (g ? D3D11_COLOR_WRITE_ENABLE_GREEN : 0) |
                    (b ? D3D11_COLOR_WRITE_ENABLE_BLUE : 0) |
                    (a ? D3D11_COLOR_WRITE_ENABLE_ALPHA : 0));
  sColorMask = m;
}

// Texturing on/off — off forces the 1x1 white texture (vertex colour only).
void gfxRenderState::SetTexEnable(bool on) { sTexEnable = on; }
void gfxRenderState::SetTexMatrix(int stage, const Matrix44 &m) {
  if (stage < 0 || stage > 1) return;
  XMMATRIX &dst = stage == 0 ? sTexMtx : sTexMtx2;
  dst = XMMATRIX(m.a.x, m.a.y, m.a.z, m.a.w, m.b.x, m.b.y, m.b.z, m.b.w,
                 m.c.x, m.c.y, m.c.z, m.c.w, m.d.x, m.d.y, m.d.z, m.d.w);
}
void vglResetTexGen() { sTexGen[0] = sTexGen[1] = 0; sTexMtx = XMMatrixIdentity(); sTexMtx2 = XMMatrixIdentity(); }
void vglSetViewportOrtho(bool ortho) { sOrthoMode = ortho; sScreenOrtho = false; sOrthoVPCamera = false; }
void vglSetScreenOrtho(bool screen) { sScreenOrtho = screen; }
void gfxRenderState::SetStencilMode(int mode) {
  sStencilMode = mode;
  static int dbg = -1; if (dbg < 0) dbg = (args::sm_Instance && ARGS.Get("shadowdebug")) ? 1 : 0;  // -shadowdebug: draw the volumes
  SetColorMask(dbg || mode != 1, dbg || mode != 1);   // volume pass touches the stencil only
}
void gfxRenderState::SetTexColorOp(int stage, gfxTextureOp op) {
  if (stage == 0) sTexColorScale = (op == texopModulate2X) ? 2.0f : 1.0f;
}

// Alpha test (shader clip).  Ref is a 0..255 threshold; func is a gfxAlphaFunc
// (alphaAlways disables the test).
void gfxRenderState::SetAlphaFunc(int func) { sAlphaFunc = func; }
void gfxRenderState::SetAlphaRef(int ref) { sAlphaRef = ref / 255.0f; }

// Explicit stencil state (D3D-compat layer).  enable=false drops back to
// mode 0 so the shadow presets and this never fight over the cache key.
void gfxRenderState::SetStencil(const gfxStencilState &s) {
  sStencil = s;
  sStencilMode = s.enable ? 3 : 0;
}
const gfxStencilState &gfxRenderState::GetStencil() const { return sStencil; }
void gfxRenderState::SetTextureAddress(int stage, gfxTexAddress u, gfxTexAddress v) {
  if (stage < 0 || stage > 1) return;
  sSamplerKey[stage].addrU = (u8)u;
  sSamplerKey[stage].addrV = (u8)v;
}
void gfxRenderState::SetTextureFilter(int stage, gfxTexFilter minMag, gfxTexFilter mip) {
  if (stage < 0 || stage > 1) return;
  sSamplerKey[stage].linear = (u8)(minMag == texfilterLinear ? 1 : 0);
  sSamplerKey[stage].mip = (u8)mip;
}
void gfxRenderState::SetMipLodBias(int stage, float bias) {
  if (stage < 0 || stage > 1) return;
  sSamplerKey[stage].bias = bias;
}
void gfxRenderState::SetTextureFactor(u32 argb) { sTexFactor = argb; }
u32 gfxRenderState::GetTextureFactor() const { return sTexFactor; }
void gfxRenderState::SetTexAlphaOp(int stage, gfxTextureOp op) {
  if (stage < 0 || stage > 1) return;
  sTexAlphaOp[stage] = op;
}

// Reset the whole immediate-mode render state to sane defaults.
void gfxRenderState::Default() {
  sTexAlphaBlendAllowed = true;
  sTexGen[0] = sTexGen[1] = 0;
  sTexMtx = XMMatrixIdentity();
  sStencilMode = 0;
  sTexSrc[0] = 0; sTexSrc[1] = 1;
  sZTestEnable = true;
  sZWriteEnable = true;
  sZFunc = zLEqual;
  sZBias = 0.0f;
  sFogEnable = false;
  sCullMode = cullNone;
  sBlendEnable = false;
  sBlendSet = blendSet_One_Zero;
  sBlendSrc = blendOne; sBlendDst = blendZero; sBlendOp = blendOpAdd;
  sScissorEnable = false;
  sFogMode = fogLinear;
  sAlphaFail = afailKeep; m_AlphaFail = afailKeep;
  sTexEnable = true;
  sColorMask = D3D11_COLOR_WRITE_ENABLE_ALL;
  ageRenderMode = renderSolid;
  m_LightingEnabled = true;
}

gfxLight gfxLight::Sun;
// gfxMaterial::FlatWhite is defined in data/main.cpp (parallel track).

extern "C" ID3D11Device *gfxGetDevice() { return sDevice; }
extern "C" ID3D11DeviceContext *gfxGetContext() { return sCtx; }

class DummyD3DDevice;
DummyD3DDevice *lpD3DDev = nullptr;
bool gfxHardwareShaders = true;

#include "gfx/vglext.h"
lowPsxGfx LOWPSXGFX;
const int MainMicrocode = 0;
gfxTexture *NoTexture = nullptr;

#include "text/stringtable.h"
txtStringTable STRINGTABLE;
gfxFont *SYSFONT_ptr = nullptr;

// ---------------------------------------------------------------------------
// 2D orthographic drawing implementations (HUD, UI, vector font)
// ---------------------------------------------------------------------------

#define MC(sx,sy,ex,ey)	((u32)(((sx&0x0F)<<12)|(sy&0x0F)<<8)|((ex&0x0F)<<4)|((ey&0x0F)))
#define GC_SX(n) ((u32)((n>>12)&0x0F))
#define GC_SY(n) ((u32)((n>>8)&0x0F))
#define GC_EX(n) ((u32)((n>>4)&0x0F))
#define GC_EY(n) ((u32)((n)&0x0F))

static u32 chUnknown[] = { 0 };
static u32 chNum0[] = { 9, MC(1,0,3,0), MC(3,0,4,2), MC(4,2,4,4), MC(4,4,3,6), MC(3,6,1,6), MC(1,6,0,4), MC(0,4,0,2), MC(0,2,1,0), MC(1,0,3,6) };
static u32 chNum1[] = { 3, MC(0,4,2,6), MC(2,6,2,0), MC(4,0,0,0) };
static u32 chNum2[] = { 9, MC(0,5,1,6), MC(1,6,3,6), MC(3,6,4,5), MC(4,5,4,3), MC(4,3,3,2), MC(3,2,1,2), MC(1,2,0,1), MC(0,1,0,0), MC(0,0,4,0) };
static u32 chNum3[] = { 11, MC(0,1,1,0), MC(1,0,3,0), MC(3,0,4,1), MC(4,1,4,2), MC(4,2,3,3), MC(3,3,1,3), MC(3,3,4,4), MC(4,4,4,5), MC(4,5,3,6), MC(3,6,1,6), MC(1,6,0,5) };
static u32 chNum4[] = { 3, MC(3,0,3,6), MC(3,6,0,2), MC(0,2,4,2) };
static u32 chNum5[] = { 8, MC(0,1,1,0), MC(1,0,3,0), MC(3,0,4,1), MC(4,1,4,3), MC(4,3,3,4), MC(3,4,0,4), MC(0,4,0,6), MC(0,6,4,6) };
static u32 chNum6[] = { 11, MC(4,5,3,6), MC(3,6,1,6), MC(1,6,0,4), MC(0,4,0,1), MC(0,1,1,0), MC(1,0,3,0), MC(3,0,4,1), MC(4,1,4,3), MC(4,3,3,4), MC(3,4,1,4), MC(1,4,0,3) };
static u32 chNum7[] = { 3, MC(2,0,4,4), MC(4,4,4,6), MC(4,6,0,6) };
static u32 chNum8[] = { 15, MC(1,3,0,2), MC(0,2,0,1), MC(0,1,1,0), MC(1,0,3,0), MC(3,0,4,1), MC(4,1,4,2), MC(4,2,3,3), MC(3,3,1,3), MC(1,3,0,4), MC(0,4,0,5), MC(0,5,1,6), MC(1,6,3,6), MC(3,6,4,5), MC(4,5,4,4), MC(4,4,3,3) };
static u32 chNum9[] = { 11, MC(0,1,1,0), MC(1,0,3,0), MC(3,0,4,1), MC(4,1,4,5), MC(4,5,3,6), MC(3,6,1,6), MC(1,6,0,5), MC(0,5,0,3), MC(0,3,1,2), MC(1,2,3,2), MC(3,2,4,3) };
static u32 chAlphaA[] = { 5, MC(0,0,0,4), MC(0,4,2,6), MC(2,6,4,4), MC(4,4,4,0), MC(0,3,4,3) };
static u32 chAlphaB[] = { 10, MC(3,3,4,4), MC(4,4,4,5), MC(4,5,3,6), MC(3,6,0,6), MC(0,6,0,0), MC(0,0,3,0), MC(3,0,4,1), MC(4,1,4,2), MC(4,2,3,3), MC(3,3,0,3) };
static u32 chAlphaC[] = { 7, MC(4,1,3,0), MC(3,0,1,0), MC(1,0,0,2), MC(0,2,0,4), MC(0,4,1,6), MC(1,6,3,6), MC(3,6,4,5) };
static u32 chAlphaD[] = { 6, MC(0,0,0,6), MC(0,6,2,6), MC(2,6,4,4), MC(4,4,4,2), MC(4,2,2,0), MC(2,0,0,0) };
static u32 chAlphaE[] = { 6, MC(4,0,0,0), MC(0,0,0,3), MC(0,3,3,3), MC(3,3,0,3), MC(0,3,0,6), MC(0,6,4,6) };
static u32 chAlphaF[] = { 5, MC(0,0,0,3), MC(0,3,3,3), MC(3,3,0,3), MC(0,3,0,6), MC(0,6,4,6) };
static u32 chAlphaG[] = { 9, MC(2,3,4,3), MC(4,3,4,1), MC(4,1,3,0), MC(3,0,1,0), MC(1,0,0,1), MC(0,1,0,5), MC(0,5,1,6), MC(1,6,3,6), MC(3,6,4,5) };
static u32 chAlphaH[] = { 5, MC(0,0,0,6), MC(0,6,0,3), MC(0,3,4,3), MC(4,3,4,6), MC(4,6,4,0) };
static u32 chAlphaI[] = { 3, MC(0,0,4,0), MC(2,0,2,6), MC(4,6,0,6) };
static u32 chAlphaJ[] = { 7, MC(0,2,0,1), MC(0,1,1,0), MC(1,0,2,0), MC(2,0,3,1), MC(3,1,3,6), MC(3,6,4,6), MC(4,6,0,6) };
static u32 chAlphaK[] = { 5, MC(0,0,0,6), MC(0,6,0,3), MC(0,3,4,6), MC(4,6,0,3), MC(0,3,4,0) };
static u32 chAlphaL[] = { 2, MC(0,6,0,0), MC(0,0,4,0) };
static u32 chAlphaM[] = { 4, MC(0,0,0,6), MC(0,6,2,3), MC(2,3,4,6), MC(4,6,4,0) };
static u32 chAlphaN[] = { 3, MC(0,0,0,6), MC(0,6,4,0), MC(4,0,4,6) };
static u32 chAlphaO[] = { 8, MC(0,1,1,0), MC(1,0,3,0), MC(3,0,4,1), MC(4,1,4,5), MC(4,5,3,6), MC(3,6,1,6), MC(1,6,0,5), MC(0,5,0,1) };
static u32 chAlphaP[] = { 6, MC(0,0,0,6), MC(0,6,3,6), MC(3,6,4,5), MC(4,5,4,4), MC(4,4,3,3), MC(3,3,0,3) };
static u32 chAlphaQ[] = { 11, MC(1,3,4,0), MC(4,0,3,1), MC(3,1,4,2), MC(4,2,4,5), MC(4,5,3,6), MC(3,6,1,6), MC(1,6,0,5), MC(0,5,0,1), MC(0,1,1,0), MC(1,0,2,0), MC(2,0,3,1) };
static u32 chAlphaR[] = { 8, MC(0,0,0,6), MC(0,6,3,6), MC(3,6,4,5), MC(4,5,4,4), MC(4,4,3,3), MC(3,3,0,3), MC(0,3,2,3), MC(2,3,4,0) };
static u32 chAlphaS[] = { 11, MC(0,1,1,0), MC(1,0,3,0), MC(3,0,4,1), MC(4,1,4,2), MC(4,2,3,3), MC(3,3,1,3), MC(1,3,0,4), MC(0,4,0,5), MC(0,5,1,6), MC(1,6,3,6), MC(3,6,4,5) };
static u32 chAlphaT[] = { 3, MC(0,6,4,6), MC(4,6,2,6), MC(2,6,2,0) };
static u32 chAlphaU[] = { 5, MC(0,6,0,1), MC(0,1,1,0), MC(1,0,3,0), MC(3,0,4,1), MC(4,1,4,6) };
static u32 chAlphaV[] = { 2, MC(0,6,2,0), MC(2,0,4,6) };
static u32 chAlphaW[] = { 4, MC(0,6,1,0), MC(1,0,2,4), MC(2,4,3,0), MC(3,0,4,6) };
static u32 chAlphaX[] = { 4, MC(0,0,4,6), MC(4,6,2,3), MC(2,3,0,6), MC(0,6,4,0) };
static u32 chAlphaY[] = { 3, MC(0,6,2,3), MC(2,3,2,0), MC(2,3,4,6) };
static u32 chAlphaZ[] = { 3, MC(0,6,4,6), MC(4,6,0,0), MC(0,0,4,0) };
static u32 chUnderline[] = { 1, MC(0,0,4,0) };
static u32 chBackSlash[] = { 1, MC(0,6,4,0) };
static u32 chExclamation[] = { 2, MC(2,6,2,2), MC(2,0,2,0) };
static u32 chDoubleQuote[] = { 2, MC(1,4,1,6), MC(3,6,3,4) };
static u32 chHash[] = { 4, MC(1,0,1,6), MC(3,0,3,6), MC(0,2,4,2), MC(0,4,4,4) };
static u32 chDollar[] = { 8, MC(1,1,3,1), MC(3,1,4,2), MC(4,2,3,3), MC(3,3,1,3), MC(1,3,0,4), MC(0,4,1,5), MC(1,5,3,5), MC(2,0,2,6) };
static u32 chPercentage[] = { 3, MC(0,0,3,6), MC(0,6,0,6), MC(3,0,3,0) };
static u32 chAmpersand[] = { 9, MC(4,3,1,0), MC(1,0,0,1), MC(0,1,0,2), MC(0,2,2,4), MC(2,4,2,5), MC(2,5,1,6), MC(1,6,0,5), MC(0,5,0,4), MC(0,4,4,0) };
static u32 chApostrophe[] = { 2, MC(2,3,3,4), MC(3,4,3,6) };
static u32 chOpenBracket[] = { 3, MC(3,0,1,2), MC(1,2,1,4), MC(1,4,3,6) };
static u32 chCloseBracket[] = { 3, MC(1,0,3,2), MC(3,2,3,4), MC(3,4,1,6) };
static u32 chAsterisk[] = { 3, MC(2,0,2,6), MC(0,1,4,5), MC(0,5,4,1) };
static u32 chPlus[] = { 2, MC(0,3,4,3), MC(2,1,2,5) };
static u32 chComma[] = { 2, MC(1,0,2,1), MC(2,1,2,2) };
static u32 chMinus[] = { 1, MC(0,3,3,3) };
static u32 chPeriod[] = { 1, MC(2,1,3,1) };
static u32 chForwardSlash[] = { 1, MC(0,0,4,6) };
static u32 chColon[] = { 2, MC(2,2,2,2), MC(2,4,2,4) };
static u32 chSemiColon[] = { 3, MC(2,4,2,4), MC(2,2,2,1), MC(2,1,1,0) };
static u32 chLessThan[] = { 2, MC(0,0,4,3), MC(4,3,0,6) };
static u32 chEqual[] = { 2, MC(1,2,3,2), MC(1,4,3,4) };
static u32 chGreaterThan[] = { 2, MC(4,0,0,3), MC(0,3,4,6) };
static u32 chQuestionMark[] = { 7, MC(2,0,2,0), MC(2,2,2,2), MC(2,2,4,4), MC(4,4,4,5), MC(4,5,3,6), MC(3,6,1,6), MC(1,6,0,5) };
static u32 chAt[] = { 9, MC(4,0,1,0), MC(1,0,0,1), MC(0,1,0,5), MC(0,5,1,6), MC(1,6,3,6), MC(3,6,4,5), MC(4,5,4,2), MC(4,2,2,2), MC(2,2,2,4) };
static u32 chOpenSquareBracket[] = { 3, MC(3,0,1,0), MC(1,0,1,6), MC(1,6,3,6) };
static u32 chCloseSquareBracket[] = { 3, MC(1,0,3,0), MC(3,0,3,6), MC(3,6,1,6) };
static u32 chHat[] = { 2, MC(2,5,3,6), MC(3,6,4,5) };
static u32 chReverseApos[] = { 1, MC(2,6,3,5) };
static u32 chOpenCurlyBrace[] = { 6, MC(3,0,2,1), MC(2,1,2,2), MC(2,2,1,3), MC(1,3,2,4), MC(2,4,2,5), MC(2,5,3,6) };
static u32 chVertBar[] = { 2, MC(2,0,2,2), MC(2,4,2,6) };
static u32 chCloseCurlyBrace[] = { 6, MC(1,0,2,1), MC(2,1,2,2), MC(2,2,3,3), MC(3,3,2,4), MC(2,4,2,5), MC(2,5,1,6) };
static u32 chTilde[] = { 3, MC(0,5,1,6), MC(1,6,2,5), MC(2,5,3,6) };

static u32 *characterSet[256] = {
    chNum0, chNum1, chNum2, chNum3, chNum4, chNum5, chNum6, chNum7,
    chNum8, chNum9, chUnknown, chUnknown, chUnknown, chUnknown, chUnknown, chUnknown,
    chUnknown, chUnknown, chUnknown, chUnknown, chUnknown, chUnknown, chUnknown, chUnknown,
    chUnknown, chUnknown, chUnknown, chUnknown, chUnknown, chUnknown, chUnknown, chUnknown,
    chUnknown, chExclamation, chDoubleQuote, chHash, chDollar, chPercentage, chAmpersand, chApostrophe,
    chOpenBracket, chCloseBracket, chAsterisk, chPlus, chComma, chMinus, chPeriod, chForwardSlash,
    chNum0, chNum1, chNum2, chNum3, chNum4, chNum5, chNum6, chNum7,
    chNum8, chNum9, chColon, chSemiColon, chGreaterThan, chEqual, chLessThan, chQuestionMark,
    chAt, chAlphaA, chAlphaB, chAlphaC, chAlphaD, chAlphaE, chAlphaF, chAlphaG,
    chAlphaH, chAlphaI, chAlphaJ, chAlphaK, chAlphaL, chAlphaM, chAlphaN, chAlphaO,
    chAlphaP, chAlphaQ, chAlphaR, chAlphaS, chAlphaT, chAlphaU, chAlphaV, chAlphaW,
    chAlphaX, chAlphaY, chAlphaZ, chOpenSquareBracket, chBackSlash, chCloseSquareBracket, chHat, chUnderline,
    chReverseApos, chAlphaA, chAlphaB, chAlphaC, chAlphaD, chAlphaE, chAlphaF, chAlphaG,
    chAlphaH, chAlphaI, chAlphaJ, chAlphaK, chAlphaL, chAlphaM, chAlphaN, chAlphaO,
    chAlphaP, chAlphaQ, chAlphaR, chAlphaS, chAlphaT, chAlphaU, chAlphaV, chAlphaW,
    chAlphaX, chAlphaY, chAlphaZ, chOpenCurlyBrace, chVertBar, chCloseCurlyBrace, chTilde, chUnknown,
    chUnknown, chUnknown, chUnknown, chUnknown, chUnknown, chUnknown, chUnknown, chUnknown,
    chUnknown, chUnknown, chUnknown, chUnknown, chUnknown, chUnknown, chUnknown, chUnknown,
    chUnknown, chUnknown, chUnknown, chUnknown, chUnknown, chUnknown, chUnknown, chUnknown,
    chUnknown, chUnknown, chUnknown, chUnknown, chUnknown, chUnknown, chUnknown, chUnknown,
    chUnknown, chUnknown, chUnknown, chUnknown, chUnknown, chUnknown, chUnknown, chUnknown,
    chUnknown, chUnknown, chUnknown, chUnknown, chUnknown, chUnknown, chUnknown, chUnknown,
    chUnknown, chUnknown, chUnknown, chUnknown, chUnknown, chUnknown, chUnknown, chUnknown,
    chUnknown, chUnknown, chUnknown, chUnknown, chUnknown, chUnknown, chUnknown, chUnknown,
    chUnknown, chUnknown, chUnknown, chUnknown, chUnknown, chUnknown, chUnknown, chUnknown,
    chUnknown, chUnknown, chUnknown, chUnknown, chUnknown, chUnknown, chUnknown, chUnknown,
    chUnknown, chUnknown, chUnknown, chUnknown, chUnknown, chUnknown, chUnknown, chUnknown,
    chUnknown, chUnknown, chUnknown, chUnknown, chUnknown, chUnknown, chUnknown, chUnknown,
    chUnknown, chUnknown, chUnknown, chUnknown, chUnknown, chUnknown, chUnknown, chUnknown,
    chUnknown, chUnknown, chUnknown, chUnknown, chUnknown, chUnknown, chUnknown, chUnknown,
    chUnknown, chUnknown, chUnknown, chUnknown, chUnknown, chUnknown, chUnknown, chUnknown,
    chUnknown, chUnknown
};

static inline u32 PackColor(float r, float g, float b, float a) {
    u32 ur = (u32)Clamp(r * 255.0f, 0.0f, 255.0f);
    u32 ug = (u32)Clamp(g * 255.0f, 0.0f, 255.0f);
    u32 ub = (u32)Clamp(b * 255.0f, 0.0f, 255.0f);
    u32 ua = (u32)Clamp(a * 255.0f, 0.0f, 255.0f);
    return (ua << 24) | (ur << 16) | (ug << 8) | ub;
}

void pipeManager::Blit2D(float x1, float y1, float x2, float y2, float u1, float v1, float u2, float v2, u32 color) {
    if (!sCtx) return;
    
    bool oldOrtho = sOrthoMode;
    bool oldRaw2D = sRaw2D;
    sRaw2D = true;
    D3D11_PRIMITIVE_TOPOLOGY oldTopology = sTopology;
    
    sOrthoMode = true;
    sTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    
    float r, g, b, a;
    ColorToFloats(color, r, g, b, a);
    
    float tw = 1.0f;
    float th = 1.0f;
    if (sBoundTex) {
        tw = (float)sBoundTex->GetWidth();
        th = (float)sBoundTex->GetHeight();
    }
    float nu1 = u1 / tw;
    float nv1 = v1 / th;
    float nu2 = u2 / tw;
    float nv2 = v2 / th;

    static int s_blitLog = 0;
    if (s_blitLog++ % 100 == 0) {
        Displayf("[BLIT2D #%d] (%.1f,%.1f)-(%.1f,%.1f) uv=(%.3f,%.3f)-(%.3f,%.3f) col=0x%08X (rgba=%.2f,%.2f,%.2f,%.2f) tex=%s (%.0fx%.0f)",
            s_blitLog, x1, y1, x2, y2, nu1, nv1, nu2, nv2, color, r, g, b, a, sBoundTex ? sBoundTex->GetName() : "null", tw, th);
    }
    
    LineVertex verts[6] = {
        { x1, y1, 0.0f, r, g, b, a, nu1, nv1 }, // TL
        { x1, y2, 0.0f, r, g, b, a, nu1, nv2 }, // BL
        { x2, y1, 0.0f, r, g, b, a, nu2, nv1 }, // TR
        
        { x2, y1, 0.0f, r, g, b, a, nu2, nv1 }, // TR
        { x1, y2, 0.0f, r, g, b, a, nu1, nv2 }, // BL
        { x2, y2, 0.0f, r, g, b, a, nu2, nv2 }  // BR
    };
    
    sVerts.Reset();
    for (int i = 0; i < 6; i++) {
        sVerts.Append(verts[i]);
    }
    
    FlushLines();
    
    sOrthoMode = oldOrtho;
    sRaw2D = oldRaw2D;
    sTopology = oldTopology;
}

void pipeManager::Blit2D(float x1, float y1, float x2, float y2, float u1, float v1, float u2, float v2, u32 color, bool flipH, bool flipV) {
    if (sCTFActive) {
        // PS2 copy-to-front blits address a framebuffer whose rows are stored
        // flipped; the flip flags in fxCopyToFront compensate for that, not
        // for anything about the image.  The PC backbuffer copy is upright,
        // so neutralise them: without this the Blur ghost pass (flipH=true on
        // PS2) overlays a horizontally mirrored copy of the frame.
        flipH = false;
        flipV = false;
    }
    if (flipH) {
        std::swap(u1, u2);
    }
    if (flipV) {
        std::swap(v1, v2);
    }
    Blit2D(x1, y1, x2, y2, u1, v1, u2, v2, color);
}

void pipeManager::ClearRect(int x, int y, int w, int h, u32 color) {
    if (!sCtx) return;
    
    bool oldOrtho = sOrthoMode;
    bool oldRaw2D = sRaw2D;
    sRaw2D = true;
    D3D11_PRIMITIVE_TOPOLOGY oldTopology = sTopology;
    gfxTexture* oldTex = sBoundTex;
    
    sOrthoMode = true;
    sTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    sBoundTex = nullptr; // force default white texture
    
    float r, g, b, a;
    ColorToFloats(color, r, g, b, a);
    
    float x1 = (float)x;
    float y1 = (float)y;
    float x2 = (float)(x + w);
    float y2 = (float)(y + h);
    
    LineVertex verts[6] = {
        { x1, y1, 0.0f, r, g, b, a, 0.0f, 0.0f },
        { x1, y2, 0.0f, r, g, b, a, 0.0f, 1.0f },
        { x2, y1, 0.0f, r, g, b, a, 1.0f, 0.0f },
        
        { x2, y1, 0.0f, r, g, b, a, 1.0f, 0.0f },
        { x1, y2, 0.0f, r, g, b, a, 0.0f, 1.0f },
        { x2, y2, 0.0f, r, g, b, a, 1.0f, 1.0f }
    };
    
    sVerts.Reset();
    for (int i = 0; i < 6; i++) {
        sVerts.Append(verts[i]);
    }
    
    FlushLines();
    
    sOrthoMode = oldOrtho;
    sRaw2D = oldRaw2D;
    sTopology = oldTopology;
    sBoundTex = oldTex;
}

static gfxBitmap *sLoadingBackdrop = NULL;

void gfxSetLoadingBackdrop(gfxBitmap *bmp) {
    if (sLoadingBackdrop && sLoadingBackdrop != bmp) sLoadingBackdrop->Release();
    sLoadingBackdrop = bmp;
}

void gfxDrawLoadingBackdrop() {
    // The loading screen repaints through here on every frame it pumps, which is the
    // one signal the engine has that a frame belongs to a load rather than to the
    // game.  Marking it keeps the -shot / -quitafter clocks frozen until the scene is
    // actually up, so "-shot x.png -shotdelay 5" captures five seconds of the scene
    // instead of landing somewhere inside a load whose length varies with what else
    // the machine is doing.  The latch is cleared at the end of every frame.
    sShotLoadingFrame = true;
    if (!sLoadingBackdrop) return;
    int x = (PIPE.GetWidth()  - sLoadingBackdrop->GetWidth())  >> 1;
    int y = (PIPE.GetHeight() - sLoadingBackdrop->GetHeight()) >> 1;
    PIPE.CopyBitmap(x, y, sLoadingBackdrop);
}

void pipeManager::CopyBitmap(int x, int y, gfxBitmap *bmp) {
    if (!bmp || !bmp->Texture || !sCtx) return;
    bool oldOrtho = sOrthoMode;
    bool oldRaw2D = sRaw2D;
    sRaw2D = true;
    D3D11_PRIMITIVE_TOPOLOGY oldTopology = sTopology;
    gfxTexture *oldTex = sBoundTex;
    sOrthoMode = true;
    sTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    sBoundTex = bmp->Texture;
    float x1 = (float)x, y1 = (float)y;
    float x2 = (float)(x + bmp->Width), y2 = (float)(y + bmp->Height);
    LineVertex verts[6] = {
        { x1, y1, 0.0f, 1, 1, 1, 1, 0.0f, 0.0f },
        { x1, y2, 0.0f, 1, 1, 1, 1, 0.0f, 1.0f },
        { x2, y1, 0.0f, 1, 1, 1, 1, 1.0f, 0.0f },
        { x2, y1, 0.0f, 1, 1, 1, 1, 1.0f, 0.0f },
        { x1, y2, 0.0f, 1, 1, 1, 1, 0.0f, 1.0f },
        { x2, y2, 0.0f, 1, 1, 1, 1, 1.0f, 1.0f }
    };
    sVerts.Reset();
    for (int i = 0; i < 6; i++) sVerts.Append(verts[i]);
    FlushLines();
    sOrthoMode = oldOrtho;
    sRaw2D = oldRaw2D;
    sTopology = oldTopology;
    sBoundTex = oldTex;
}

void pipeManager::ClearRect(int x, int y, int w, int h, const class Vector4 &color) {
    u32 c = PackColor(color.x, color.y, color.z, color.w);
    ClearRect(x, y, w, h, c);
}

extern "C" void gfxDrawPolyline2D(const float *xy, int numPoints,
                                  unsigned int color) {
    if (!xy || numPoints < 2 || !sCtx) return;

    bool oldOrtho = sOrthoMode;
    bool oldRaw2D = sRaw2D;
    sRaw2D = true;
    D3D11_PRIMITIVE_TOPOLOGY oldTopology = sTopology;
    gfxTexture* oldTex = sBoundTex;

    sOrthoMode = true;
    sTopology = D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;
    sBoundTex = nullptr; // force default white texture

    float r, g, b, a;
    ColorToFloats(color, r, g, b, a);

    if (numPoints > MAX_VERTS) numPoints = MAX_VERTS;

    sVerts.Reset();
    for (int i = 0; i < numPoints; i++) {
        LineVertex v = { xy[i * 2], xy[i * 2 + 1], 0.0f, r, g, b, a, 0.0f, 0.0f };
        sVerts.Append(v);
    }

    FlushLines();

    sOrthoMode = oldOrtho;
    sRaw2D = oldRaw2D;
    sTopology = oldTopology;
    sBoundTex = oldTex;
}

// --- picking / projection helpers (vgl.h) ----------------------------------

static XMMATRIX BuildViewProj() {
  XMMATRIX projMatrix = sProj;
  if (pipeManager::sm_Instance && pipeManager::sm_Instance->GetViewport()) {
    const Matrix44 &proj = pipeManager::sm_Instance->GetViewport()->GetProjection();
    projMatrix = XMMATRIX(proj.m[0], proj.m[1], proj.m[2], proj.m[3], proj.m[4],
                          proj.m[5], proj.m[6], proj.m[7], proj.m[8], proj.m[9],
                          proj.m[10], proj.m[11], proj.m[12], proj.m[13],
                          proj.m[14], proj.m[15]);
  }
  return sView * projMatrix;
}

// The current viewport window (screen pixels); the whole target when none.
static void CurrentWindow(float &x, float &y, float &w, float &h) {
  x = 0.0f; y = 0.0f; w = (float)sWidth; h = (float)sHeight;
  const gfxViewport *vp = pipeManager::sm_Instance ? pipeManager::sm_Instance->GetViewport() : NULL;
  if (vp) {
    const gfxViewportParams &p = vp->GetViewportParams();
    if (p.m_Width > 0.0f && p.m_Height > 0.0f) { x = p.m_X; y = p.m_Y; w = p.m_Width; h = p.m_Height; }
  }
}

bool vglProject(const Vector3 &world, float &screenX, float &screenY) {
  XMVECTOR v = XMVector4Transform(XMVectorSet(world.x, world.y, world.z, 1.0f), BuildViewProj());
  float w = XMVectorGetW(v);
  if (w <= 1e-6f)
    return false;
  float wx, wy, ww, wh;
  CurrentWindow(wx, wy, ww, wh);
  screenX = wx + (XMVectorGetX(v) / w * 0.5f + 0.5f) * ww;
  screenY = wy + (1.0f - (XMVectorGetY(v) / w * 0.5f + 0.5f)) * wh;
  return true;
}

void vglComputePickRay(float screenX, float screenY, Vector3 &nearPt, Vector3 &farPt) {
  XMVECTOR det;
  XMMATRIX inv = XMMatrixInverse(&det, BuildViewProj());
  float wx, wy, ww, wh;
  CurrentWindow(wx, wy, ww, wh);
  float nx = (screenX - wx) / ww * 2.0f - 1.0f;
  float ny = 1.0f - (screenY - wy) / wh * 2.0f;
  XMVECTOR a = XMVector4Transform(XMVectorSet(nx, ny, 0.0f, 1.0f), inv);  // D3D near plane (z=0)
  XMVECTOR b = XMVector4Transform(XMVectorSet(nx, ny, 1.0f, 1.0f), inv);  // far plane (z=1)
  float wa = XMVectorGetW(a), wb = XMVectorGetW(b);
  if (fabsf(wa) < 1e-9f || fabsf(wb) < 1e-9f) {
    nearPt.Set(0.0f, 0.0f, 0.0f);
    farPt.Set(0.0f, 0.0f, 1.0f);
    return;
  }
  nearPt.Set(XMVectorGetX(a) / wa, XMVectorGetY(a) / wa, XMVectorGetZ(a) / wa);
  farPt.Set(XMVectorGetX(b) / wb, XMVectorGetY(b) / wb, XMVectorGetZ(b) / wb);
}

void vglDrawLabelf(const Vector3 &pos, int dx, int dy, const char *fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  float sx, sy;
  if (vglProject(pos, sx, sy))
    gfxDrawFont((int)sx + dx, (int)sy + dy, buf);
}

void vglDrawLabelf(const Vector3 &pos, const char *fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  float sx, sy;
  if (vglProject(pos, sx, sy))
    gfxDrawFont((int)sx, (int)sy, buf);
}

void vglDrawLabel(const Vector3 &pos, int dx, int dy, const char *text) {
  float sx, sy;
  if (text && vglProject(pos, sx, sy))
    gfxDrawFont((int)sx + dx * gfxFontGetWidth(), (int)sy + dy * gfxFontGetHeight(), text);
}

void vglDrawLabel(const Vector3 &pos, const char *text) {
  vglDrawLabel(pos, 0, 0, text);
}

// PC PORT: stroke text is QUEUED and flushed after the copy-to-front pass
// (gfxEndFrame).  On the PS2 an immediate mid-frame gfxDrawFont landed on the
// FRONT buffer - i.e. after the CTF blur/modulate by architecture - so script
// drawtext, HUD readouts and debug overlays were never blurred.  Drawing them
// immediately into the D3D11 backbuffer put them UNDER the motion-blur echo
// (FXCathedral scenario briefing text).
struct sQueuedText {
    int x, y;
    unsigned int color;
    float scale;
    char text[256];
};
static atArray<sQueuedText> sTextQueue;

static void DrawFontNow(int x, int y, const char *text, unsigned int color, float scale) {
    if (!text || !*text || !sCtx) return;

    
    bool oldOrtho = sOrthoMode;
    bool oldRaw2D = sRaw2D;
    sRaw2D = true;
    D3D11_PRIMITIVE_TOPOLOGY oldTopology = sTopology;
    gfxTexture* oldTex = sBoundTex;
    
    sOrthoMode = true;
    sTopology = D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
    sBoundTex = nullptr; // force default white texture
    
    float r, g, b, a;
    ColorToFloats(color, r, g, b, a);
    
    float cx = (float)x;
    float cy = (float)y;
    
    float scaleX = scale;
    float scaleY = scale;
    
    sVerts.Reset();
    
    for (const char *p = text; *p; p++) {
        unsigned char ch = (unsigned char)*p;
        u32 *strokeData = (ch < 256) ? characterSet[ch] : nullptr;
        if (!strokeData) continue;
        
        u32 numLines = *strokeData;
        u32 *strokes = strokeData + 1;
        
        for (u32 i = 0; i < numLines; i++) {
            u32 stroke = strokes[i];
            float sx = cx + (float)GC_SX(stroke) * scaleX;
            float sy = cy + (float)(6 - GC_SY(stroke)) * scaleY;
            float ex = cx + (float)GC_EX(stroke) * scaleX;
            float ey = cy + (float)(6 - GC_EY(stroke)) * scaleY;
            
            LineVertex v1 = { sx, sy, 0.0f, r, g, b, a, 0.0f, 0.0f };
            LineVertex v2 = { ex, ey, 0.0f, r, g, b, a, 0.0f, 0.0f };
            
            sVerts.Append(v1);
            sVerts.Append(v2);
        }
        
        cx += 6.0f * scaleX;
    }

    FlushLines();

    sOrthoMode = oldOrtho;
    sRaw2D = oldRaw2D;
    sTopology = oldTopology;
    sBoundTex = oldTex;
}

extern "C" void gfxDrawFontScaled(int x, int y, const char *text, unsigned int color, float scale) {
    if (!text || !*text || !sCtx) return;
    sQueuedText &q = sTextQueue.Append();
    q.x = x;
    q.y = y;
    q.color = color;
    q.scale = scale;
    strncpy(q.text, text, sizeof(q.text) - 1);
    q.text[sizeof(q.text) - 1] = '\0';
}

extern "C" void gfxDrawFont(int x, int y, const char *text, unsigned int color) {
    gfxDrawFontScaled(x, y, text, color, 1.4f);
}

// Called from gfxEndFrame after the copy-to-front pass (and before the
// screenshot saves, so captures include the text).
static void FlushQueuedText() {
    for (int i = 0; i < sTextQueue.GetCount(); i++)
        DrawFontNow(sTextQueue[i].x, sTextQueue[i].y, sTextQueue[i].text,
                    sTextQueue[i].color, sTextQueue[i].scale);
    sTextQueue.Reset();
}

// Second handle on a texture's GPU resources (d3dcompat surfaces).  Each
// COM object gains a reference so both gfxTexture destructors balance.
void gfxAliasTexture(gfxTexture *dst, const gfxTexture *src) {
  if (!dst || !src || dst == src) return;
  dst->Width = src->Width;
  dst->Height = src->Height;
  dst->D3DTexture = src->D3DTexture;
  dst->SRV = src->SRV;
  dst->RTV = src->RTV;
  dst->m_HasAlpha = src->m_HasAlpha;
  dst->m_AllTranslucent = src->m_AllTranslucent;
  if (dst->D3DTexture) dst->D3DTexture->AddRef();
  if (dst->SRV) dst->SRV->AddRef();
  if (dst->RTV) dst->RTV->AddRef();
}
