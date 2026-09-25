////////////////////////////////////////
// rstate.h
//
// Render state manager (RSTATE) + the matrix stack (MTX), lights and materials.
// Interface reconstructed from testanim2 usage; the D3D11 backend fills the
// bodies (rasterizer/blend/depth states, lighting, transforms).
////////////////////////////////////////

#ifndef GFX_RSTATE_H
#define GFX_RSTATE_H

#include "core/output.h"
#include "core/types.h"
#include "vector/Matrix34.h"
#include "vector/matrix44.h"
#include "gfx/vgl.h"
#include "gfx/statetypes.h"
#include "gfx/texture.h"

////////////////////////////////////////////////////////////////////////////
// Lights / materials — a couple of well-known statics are referenced directly.

class gfxLight
{
public:
	static gfxLight Sun;
	int type = lightDirectional;
	Vector3 dir;
	gfxColor Color;
	Vector4 diffuse;
	Vector4 ambient;
	Vector4 specular;
	// Point/spot terms (D3DLIGHT layout): range cutoff, constant/linear/
	// quadratic attenuation, spot falloff.  Ignored for directional lights.
	float range = 1000.0f;
	float atten0 = 1.0f;
	float atten1 = 0.0f;
	float atten2 = 0.0f;
	float falloff = 1.0f;
	float theta = 0.0f;
	float phi = 0.0f;
};

#include "gfx/misc.h"

////////////////////////////////////////////////////////////////////////////
// MTX — the transform (matrix) stack.  MTX[i] is a Matrix34.

class gfxMatrixStack
{
public:
	Matrix34 &operator[](int i)				{return Stack[i];}
	const Matrix34 &operator[](int i) const	{return Stack[i];}

	// Decay to the underlying array so consumers (e.g. crSkeleton::Attach) can
	// take a plain Matrix34* without depending on gfx.
	operator Matrix34*()					{return Stack;}
	operator const Matrix34*() const		{return Stack;}

	Matrix34 Stack[64];
};

extern gfxMatrixStack MTX;

////////////////////////////////////////////////////////////////////////////
// RSTATE — the global render-state manager.

class gfxRenderState
{
public:
	gfxRenderState() : m_LightingEnabled(true), m_LightingMode(1), m_ActiveMaterial(nullptr) {}

	void SetLighting(bool on);
	bool GetLighting() const { return m_LightingEnabled; }
	bool GetLightEnable(int index) const;
	// The direction TOWARDS light `index` and its colour; false when that slot is
	// off or not directional (CPU vertex shading of vehicle materials).
	bool GetDirectionalLight(int index, Vector3 &towardsLight, Vector3 &color) const;
	void GetAmbientColor(Vector3 &color) const;
	// Raw light slot for diagnostics: mode (0 off, 1 directional, 2 point, 3 fx directional), direction or position, colour.
	void GetLightSlot(int index, int &mode, Vector3 &dirOrPos, Vector3 &color) const;
	void SetLightingMode(int mode);
	int GetLightingMode() const { return m_LightingMode; }
	void SetLight(int index,const gfxLight *light);
	void SetIdentity();
	void SetWorld(const Matrix34 &m);
	const Matrix34 & GetWorld() const { return m_World; }
	void SetWorld(const Matrix44 &m);
	void SetCamera(const Matrix34 &m);
	void SetCamera(const Matrix44 &m);
	void SetMaterial(const gfxMaterial *mat);
	const gfxMaterial * GetMaterial() const { return m_ActiveMaterial; }
	void SetForceColor(unsigned long color);
	void SetColor(unsigned long color) { SetForceColor(color); }
	void SetColor(const gfxColor &c) { SetForceColor(c); }
	void SetBaseColor(unsigned long color) {
		float r = ((color >> 16) & 0xff) / 255.0f;
		float g = ((color >> 8) & 0xff) / 255.0f;
		float b = (color & 0xff) / 255.0f;
		float a = ((color >> 24) & 0xff) / 255.0f;
		SetBaseColor(r, g, b, a);
	}
	// Base colour: the colour a draw gets when it supplies none (immediate-mode
	// current colour); unlike SetForceColor it does not override vertex colours.
	void SetBaseColor(const gfxColor &c);
	void SetBaseColor(float r, float g, float b, float a = 1.0f);
	void SetTexture(const gfxTexture *tex);
	// Stage 1 is the backend's second sampler (vglBindTexture2): its combine
	// comes from vglTex2Combine/vglTex2CombineOps, plain modulate by default.
	void SetTexture(int stage, const gfxTexture *tex);
	const gfxTexture *GetTexture() const;
	const gfxTexture *GetTexture(int stage) const;
	// UV transform for a stage; game code passes NULL to reset it.
	void SetTextureMatrix(int stage, const Matrix34 &m);
	void SetTextureMatrix(const Matrix34 &m) { SetTextureMatrix(0, m); }
	void SetZFunc(EnumZFunc func);
	EnumZFunc GetZFunc() const;
	void SetZTestEnable(bool on);
	void SetZWriteEnable(bool on);
	void SetFogEnable(bool on);
	// Fog curve: fogNone disables, fogLinear ramps start..end, fogExp/fogExp2
	// use the density curve (density = 1 / (end - start)).
	void SetFogMode(int mode);
	int GetFogMode() const;
	void SetFogStart(float start);
	void SetFogEnd(float end);
	void SetFogColor(unsigned long color);
	// Distance fog (PS2 per-vertex fog / full-screen palette fog approximated
	// per pixel in the shader).  color 0xAARRGGBB; fog amount ramps from start
	// to end (view depth) and is clamped to [min,max]; power > 0 selects the
	// palette-fog curve (lvlFogData::CreatePalette), 0 = linear.
	void SetFogParams(unsigned long color, float start, float end, float min, float max);
	void SetFogPower(float power);
	// Vertex lighting (rmcLightGroup: ambient + up to 3 lights).  A light with a
	// non-zero Dir is directional; otherwise it is a point light at Pos.
	void SetLights(const class rmcLightGroup &lights);
	void LightEnable(int index, bool enable);
	void SetZBias(float bias);
	float GetZBias() const;
	void SetFillMode(EnumFillMode mode);
	void SetAlphaBlendEnable(bool on);
	void SetAlphaBlend(bool on) { SetAlphaBlendEnable(on); }
	bool GetAlphaBlendEnable() const;
	void SetBlendSet(EnumBlendSet set);
	EnumBlendSet GetBlendSet() const;
	// Whether a model draw may turn alpha blending / alpha test on for a texture
	// that carries alpha.  Off while a caller has selected an opaque blend set
	// (rmcState::SetBlendSet ONE/ZERO); back on at Default().
	void SetTextureAlphaBlendAllowed(bool allow);
	bool GetTextureAlphaBlendAllowed() const;
	void SetCull(gfxCullMode mode);
	gfxCullMode GetCull() const;
	bool GetZTestEnable() const;
	bool GetZWriteEnable() const;
	bool GetFogEnable() const;
	const Matrix34 & GetCamera() const { return m_Camera; }
	const Vector3 & GetCameraPosition() const { return m_Camera.d; }
	void Default();
	// Immediate-mode backend: every vglEnd() submits its batch, so there is
	// no queued work to flush.
	// The D3D11 backend applies blend / depth / raster / sampler state
	// immediately before each Draw, so there is nothing queued to push here.
	// (On the console this flushed the deferred GS register writes.)
	void Flush() { }

	// Scissor rectangle in screen pixels (x, y, w, h), applied to every
	// draw on top of the viewport window.  w or h <= 0 clears it.
	void SetScissor(int x, int y, int w, int h);
	void ClearScissor();
	bool GetScissor(int &x, int &y, int &w, int &h) const;

	void SetColorMask(bool rgb, bool alpha);
	void SetColorMask(bool r, bool g, bool b, bool a);
	void SetTexEnable(bool enable);
	void SetAlphaFunc(int func);   // alpha test (shader clip); alphaAlways disables
	gfxAlphaFunc GetAlphaFunc() const;
	void SetAlphaRef(int ref);     // 0..255 threshold
	int GetAlphaRef() const;
	void SetForceColor(const gfxColor &color);

	void SetAmbient(gfxColor c);
	void SetAmbient(gfxPackedColor c);
	// Individual blend factors / equation.  SetBlendSet selects a preset pair;
	// SetSrcBlend/SetDestBlend/SetBlendOp override the factors one at a time
	// (the D3D11 backend builds a blend state per distinct combination).
	void SetSrcBlend(gfxBlendFunc func);
	void SetDestBlend(gfxBlendFunc func);
	gfxBlendFunc GetSrcBlend() const;
	gfxBlendFunc GetDestBlend() const;
	void SetBlendOp(int op);   // gfxBlendOp
	int GetBlendOp() const;
	void SetTexMatrix(int stage, const Matrix44 &mtx);   // stage 0/1 UV transform, applied when that stage's SetTexGeneration mode is 2
	// mode 0 = vertex UV set `arg2` (shader "texsrc N"); mode 2 = UV * texture matrix (scroll/scale). Env/reflection modes are not supported.
	void SetTexGeneration(int stage, int mode, bool arg1, int arg2, int arg3);
	void SetTexColorOp(int stage, gfxTextureOp op);   // stage 0: texopModulate2X doubles the texture*colour result
	// Stencil: 0 off; 1 = shadow-volume count (two-sided incr/decr, depth test, no colour/depth writes);
	// 2 = draw only where stencil != 0 (no depth test).
	void SetStencilMode(int mode);
	// Explicit stencil state (D3D-compat layer); enable=false is mode 0.
	void SetStencil(const gfxStencilState &s);
	const gfxStencilState &GetStencil() const;
	// Per-stage sampler control: address wrap/clamp/mirror, point/linear
	// filtering, mip filter (texfilterNone = no mips) and LOD bias.  A
	// texture's own gfxTexEnvClampU/V flags force clamp regardless.
	void SetTextureAddress(int stage, gfxTexAddress u, gfxTexAddress v);
	void SetTextureFilter(int stage, gfxTexFilter minMag, gfxTexFilter mip);
	void SetMipLodBias(int stage, float bias);
	// Fixed-function constant colour (D3DTA_TFACTOR) and the stage alpha op.
	// Recorded for the compat layer; the single-shader backend always
	// modulates texture alpha by vertex alpha.
	void SetTextureFactor(u32 argb);
	u32 GetTextureFactor() const;
	void SetTexAlphaOp(int stage, gfxTextureOp op);
	// Alpha-fail routing (PS2 GS AFAIL).  D3D11 cannot route a failed alpha
	// test to a different write mask per pixel; the backend approximates the
	// two colour-preserving modes by passing the fragment with the depth
	// write disabled (afailFBOnly/afailRGBOnly) and afailZOnly by passing it
	// with the colour write disabled -- see GetDepthState/GetBlendState.
	void SetAlphaFail(int mode);
	int GetAlphaFail() const { return m_AlphaFail; }
	// Destination-alpha test (PS2 GS DATE): recorded for GetDestAlpha* readers;
	// no per-pixel dest-alpha test exists on the backend.
	void SetDestAlphaEnable(bool enable) { m_DestAlphaEnable = enable; }
	bool GetDestAlphaEnable() const { return m_DestAlphaEnable; }
	void SetDestAlphaMode(int mode) { m_DestAlphaMode = mode; }
	int GetDestAlphaMode() const { return m_DestAlphaMode; }
	void DisableAllLights();

	// --- PS2 hardware knobs with no D3D11 counterpart ----------------------------
	// Intentionally empty (the state has no meaning on this backend, not
	// "unfinished"): SetDither = GS dither matrix (UNORM8 targets don't dither);
	// SetColorClamp = GS COLCLAMP (UNORM outputs always clamp); SetGS pokes a
	// raw GS register; SetVu0Enable toggles the PS2 VU0 transform path.
	void SetDither(bool on) { (void)on; }
	void SetColorClamp(bool enable) { (void)enable; }
	bool GetColorClamp() const { return true; }
	void SetGS(u64 reg, u64 val) { (void)reg; (void)val; }
	void SetVu0Enable(bool on) { (void)on; }
	// ---------------------------------------------------------------------------
	// World * view * projection of the current viewport, row-vector layout:
	// Matrix44::Transform(worldPos, clip) then clip.xy / clip.w is the D3D
	// normalised device position (the game projects glows, sparks and LED
	// panels to the screen with it).  Rebuilt from the live state on each call.
	const class Matrix44 & GetComposite() const;
	// Inverse of the camera (world -> view space).
	const Matrix34 & GetView() const;

private:
	bool m_LightingEnabled;
	int m_AlphaFail = 0;
	bool m_DestAlphaEnable = false;
	int m_DestAlphaMode = 0;
	int m_LightingMode;
	const class gfxMaterial *m_ActiveMaterial;
	mutable class Matrix44 m_Composite;
	mutable Matrix34 m_View;
	Matrix34 m_Camera;
	Matrix34 m_World;
};

extern gfxRenderState RSTATE;

// Scoped save/restore of one render state (AGE 2.72 particle systems bracket
// their draws with these): RSTATE_PUSH(type, Name, value) declares a local
// holding RSTATE.GetName(), then applies value; RSTATE_POP(Name) restores
// it.  Both must sit in the same block.
#define RSTATE_PUSH(type, name, val)	type rstatePushed_##name = (type)RSTATE.Get##name(); RSTATE.Set##name(val)
#define RSTATE_POP(name)				RSTATE.Set##name(rstatePushed_##name)

#endif // GFX_RSTATE_H
