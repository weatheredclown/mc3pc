////////////////////////////////////////
// rscview/rscview.cpp
//
// Resource pack viewer: loads a console .pck, decodes the rmcModel
// geometry it holds (rmcore/rscgeom.h) and draws it under an orbit camera,
// so the decoder can be judged from a screenshot.
//
//   rscview -path <assets> -pack resources/city/sd_midnight_clear
//           [-model <n> | -models <first> <count>] [-part <0..4>] [-wire]
//           [-list] [-shot out.png -shotframes 3]
//
// -shot/-shotframes/-quitafter are the gfx backend's own capture flags.
//   -unlit draws the flat material colours; -noground drops the disc + contact shadow.
//
//   rscview -path <assets> -ambient va_civic_sh [-bodycolor N] [-list]
//           an ambient (traffic) vehicle through the game's own loader: see RunAmbientViewer.
//
//   rscview -path <assets> -ped [name|index] [-city sd] [-anim walk] [-list]
//           one city pedestrian out of <city>_peds.pck playing a clip from ped/anim:
//           see RunPedViewer.
////////////////////////////////////////

#include "core/output.h"
#include "core/stream.h"
#include "data/args.h"
#include "data/assetcfg.h"
#include "data/main.h"
#include "data/rscimage.h"
#include "data/timemgr.h"
#include "devcam/polarcam.h"
#include "gfx/rstate.h"
#include "gfx/simple.h"
#include "gfx/texture.h"
#include "gfx/vgl.h"
#include "memory/heap.h"
#include "zipfile/zipfile.h"
#include "data/pager.h"
#include "rmcore/rscgeom.h"
#include "mclevel/level.h"
#include "mclevel/citymodel.h"
#include "mclevel/instcitymodel.h"
#include "mclevel/hood.h"
#include "mclevel/conditions.h"
#include "mcgfx/mcgfx.h"
#include "mcdata/config.h"
#include "data/param.h"
#include "atl/bitset.h"
#include "crskeleton/skeldata.h"
#include "crskeleton/skeleton.h"
#include "data/hash.h"
#include "data/token.h"
#include "gfx/model.h"
#include "mcgfx/drawable.h"
#include "rmcore/light.h"
#include "rmcore/state.h"
#include "rmcore/drawable.h"
// The ped viewer plays clips through whichever animation library the game code is
// built with: mcped picks crfanimation (fixed point) when __USE_CRFANIMATION_LIB is set.
#include "crfanimation/animation.h"
#include "crfanimation/frame.h"
typedef crfAnimation rscAnimation;
typedef crfAnimFrame rscAnimFrame;
#include "mcped/CreatureTypeManager.h"
#include "mcped/CreatureType.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include "gfx/font.h"
#include "input/keyboard.h"
#include "input/keys.h"

PARAM(nofe, "Skip the frontend");

static atArray<rscModel> s_Models;
static Vector3 s_Min, s_Max;
static bool s_Wire = false;
static int s_Part = -1;
static int s_Prim = 0;      // 0 strip, 1 list, 2 fan

struct rscViewerMatInfo {
	u32 color;          // RGBA (mkrgba): base colour, or the modulate colour when textured
	bool alphaBlend;
	gfxTexture *texture;
	// Shading (software, baked into the vertex colour each frame)
	float spec;         // specular strength (0 = none)
	float gloss;        // specular exponent
	float reflect;      // environment (sky gradient) reflection strength
	bool metallic;      // environment tinted by the base colour (chrome, alloy)
	bool emissive;      // lights: mostly unlit
	bool glass;         // fresnel adds to alpha
	bool vertexColor;   // city packs: the baked vertex colour is the lit colour (0x80 = 1.0)
	bool isChrome;      // chrome reflection mapping (texgen reflect with envmap)
	bool invAlpha = false;   // city_window_cutout: dest weighted by source alpha (InvSrcAlpha, SrcAlpha)
	int alphaRef = 0;        // > 0: alpha test "greater than" this (0..255) in the blend pass
	gfxTexture *layer = nullptr;   // city_window layer (tinted, combine 2) or city_road detail (combine 3)
	int layerCombine = 2;          // vglTex2Combine mode for the layer
	float layerScale[2] = { 0.0f, 0.0f };   // non-zero: layer UV = first UV set * scale (city_road)
	bool layerReflect = false;   // stage-2 texgen reflect (city_window_daytime)
};

static atArray<rscViewerMatInfo> s_CarMaterials;
static atArray<rscViewerMatInfo> s_RimMaterials;
static atArray<rscShaderInfo> carShaders;
static bool s_IsCarAssemble = false;
static bool s_IsBike = false;        // two wheel records in the pack: motorcycle
static atArray<rscBone> s_Bones;     // the pack's skeleton (parts are bone-local)
static rscCarKits s_Kits;            // which bones carry kit variants (rmcCarModelType tables)
static bool s_HaveKits = false;
static bool s_Lit = true;
static Vector3 s_CamPos(0.0f, 0.0f, 0.0f);
static Vector3 s_LightDir(0.3f, 0.9f, 0.3f);     // towards the key light
static Vector3 s_Center(0.0f, 0.0f, 0.0f);

static inline void SetMatShading(rscViewerMatInfo &m, float spec, float gloss, float reflect, bool metallic = false,
                                 bool emissive = false, bool glass = false, bool isChrome = false)
{
	m.spec = spec; m.gloss = gloss; m.reflect = reflect; m.metallic = metallic; m.emissive = emissive; m.glass = glass;
	m.isChrome = isChrome;
	m.vertexColor = false;
}

static gfxTexture *GetCarEnvMapTexture()
{
	static gfxTexture *s_EnvTex = NULL;
	if (s_EnvTex) return s_EnvTex;
	const char *envArg = 0;
	if (ARGS.Get("envmap", 0, &envArg) && envArg && envArg[0]) {
		s_EnvTex = gfxGetTexture(envArg, true, false);
	}
	if (!s_EnvTex) s_EnvTex = gfxGetTexture("fx_car_viewer_envmap", true, false);
	if (!s_EnvTex) s_EnvTex = gfxGetTexture("__envmap__", true, false);
	return s_EnvTex;
}

static gfxTexture *GetLicensePlateTexture()
{
	static gfxTexture *s_PlateTex = NULL;
	if (s_PlateTex) return s_PlateTex;

	const int W = 256, H = 128;
	atArray<u8> rgba;
	rgba.Resize(W * H * 4);

	auto set_pixel = [&](int x, int y, u8 r, u8 g, u8 b, u8 a = 255) {
		// Flip Y to match D3D top-to-bottom V texture coordinate convention
		y = H - 1 - y;
		if (x >= 0 && x < W && y >= 0 && y < H) {
			int idx = (y * W + x) * 4;
			rgba[idx] = r;
			rgba[idx + 1] = g;
			rgba[idx + 2] = b;
			rgba[idx + 3] = a;
		}
	};

	auto fill_rect = [&](int x0, int y0, int x1, int y1, u8 r, u8 g, u8 b) {
		for (int y = y0; y < y1; y++)
			for (int x = x0; x < x1; x++)
				set_pixel(x, y, r, g, b);
	};

	// 1. Fill base off-white / warm ivory plate background
	for (int y = 0; y < H; y++) {
		for (int x = 0; x < W; x++) {
			set_pixel(x, y, 245, 246, 248);
		}
	}

	// 2. Embossed plate rim border
	for (int y = 0; y < H; y++) {
		for (int x = 0; x < W; x++) {
			int dx = x < (W - 1 - x) ? x : (W - 1 - x);
			int dy = y < (H - 1 - y) ? y : (H - 1 - y);
			int d = dx < dy ? dx : dy;
			if (d < 3) {
				set_pixel(x, y, 35, 45, 60);
			} else if (d == 3 || d == 4) {
				set_pixel(x, y, 220, 225, 230);
			} else if (d == 5 || d == 6) {
				set_pixel(x, y, 175, 185, 195);
			}
		}
	}

	// 3. Bolt holes
	static const int bolts[4][2] = {{24, 18}, {W - 25, 18}, {24, H - 19}, {W - 25, H - 19}};
	for (int b = 0; b < 4; b++) {
		int bx = bolts[b][0], by = bolts[b][1];
		for (int dy = -4; dy <= 4; dy++) {
			for (int dx = -4; dx <= 4; dx++) {
				int r2 = dx * dx + dy * dy;
				if (r2 <= 16) {
					if (r2 <= 9) {
						u8 c = (dx == 0 || dy == 0) ? 70 : 160;
						set_pixel(bx + dx, by + dy, c, c, c + 10);
					} else {
						set_pixel(bx + dx, by + dy, 40, 45, 55);
					}
				}
			}
		}
	}

	// Fonts definition for bitmaps
	static const char *font3x5[9][5] = {
		{"111", "100", "100", "100", "111"}, // C
		{"010", "101", "111", "101", "101"}, // A
		{"100", "100", "100", "100", "111"}, // L
		{"111", "010", "010", "010", "111"}, // I
		{"111", "100", "110", "100", "100"}, // F
		{"111", "101", "101", "101", "111"}, // O
		{"110", "101", "110", "101", "101"}, // R
		{"101", "111", "111", "101", "101"}, // N
		{"000", "000", "000", "000", "000"}  // space
	};

	auto draw_char3x5 = [&](char ch, int x, int y, int scale, u8 r, u8 g, u8 b) {
		int idx = 8;
		switch (ch) {
			case 'C': idx = 0; break; case 'A': idx = 1; break; case 'L': idx = 2; break;
			case 'I': idx = 3; break; case 'F': idx = 4; break; case 'O': idx = 5; break;
			case 'R': idx = 6; break; case 'N': idx = 7; break; default: idx = 8; break;
		}
		for (int row = 0; row < 5; row++) {
			for (int col = 0; col < 3; col++) {
				if (font3x5[idx][row][col] == '1') {
					fill_rect(x + col * scale, y + row * scale, x + (col + 1) * scale, y + (row + 1) * scale, r, g, b);
				}
			}
		}
	};

	auto draw_string3x5 = [&](const char *str, int x, int y, int scale, u8 r, u8 g, u8 b) {
		int cur_x = x;
		for (int i = 0; str[i]; i++) {
			draw_char3x5(str[i], cur_x, y, scale, r, g, b);
			cur_x += (3 + 1) * scale;
		}
	};

	// 5x7 font
	static const char *font5x7[24][7] = {
		{"01110", "10001", "10001", "11111", "10001", "10001", "10001"}, // 0: A
		{"11110", "10001", "10001", "11110", "10001", "10001", "11110"}, // 1: B
		{"01111", "10000", "10000", "10000", "10000", "10000", "01111"}, // 2: C
		{"11110", "10001", "10001", "10001", "10001", "10001", "11110"}, // 3: D
		{"11111", "10000", "10000", "11110", "10000", "10000", "11111"}, // 4: E
		{"11111", "10000", "10000", "11110", "10000", "10000", "10000"}, // 5: F
		{"01111", "10000", "10000", "10011", "10001", "10001", "01110"}, // 6: G
		{"10001", "10001", "10001", "11111", "10001", "10001", "10001"}, // 7: H
		{"11111", "00100", "00100", "00100", "00100", "00100", "11111"}, // 8: I
		{"10000", "10000", "10000", "10000", "10000", "10000", "11111"}, // 9: L
		{"10001", "11011", "10101", "10101", "10001", "10001", "10001"}, // 10: M
		{"10001", "11001", "10101", "10011", "10001", "10001", "10001"}, // 11: N
		{"01110", "10001", "10001", "10001", "10001", "10001", "01110"}, // 12: O
		{"11110", "10001", "10001", "11110", "10000", "10000", "10000"}, // 13: P
		{"11110", "10001", "10001", "11110", "10100", "10010", "10001"}, // 14: R
		{"01111", "10000", "10000", "01110", "00001", "00001", "11110"}, // 15: S
		{"11111", "00100", "00100", "00100", "00100", "00100", "00100"}, // 16: T
		{"10001", "10001", "10001", "10001", "10001", "10001", "01110"}, // 17: U
		{"01110", "10011", "10101", "10101", "11001", "10001", "01110"}, // 18: 0
		{"11110", "00001", "00001", "01110", "00001", "00001", "11110"}, // 19: 3
		{"11111", "10000", "10000", "11110", "00001", "00001", "11110"}, // 20: 5
		{"00000", "00000", "00000", "00000", "00000", "00000", "00000"}, // 21: ' '
		{"00000", "00000", "00000", "11111", "00000", "00000", "00000"}, // 22: '-'
		{"10001", "10001", "01010", "00100", "01010", "10001", "10001"}  // 23: K
	};

	auto font5x7_idx = [](char ch) -> int {
		switch (ch) {
			case 'A': return 0; case 'B': return 1; case 'C': return 2; case 'D': return 3;
			case 'E': return 4; case 'F': return 5; case 'G': return 6; case 'H': return 7;
			case 'I': return 8; case 'L': return 9; case 'M': return 10; case 'N': return 11;
			case 'O': return 12; case 'P': return 13; case 'R': return 14; case 'S': return 15;
			case 'T': return 16; case 'U': return 17; case '0': return 18; case '3': return 19;
			case '5': return 20; case ' ': return 21; case '-': return 22; case 'K': return 23;
			default: return 21;
		}
	};

	auto draw_string5x7 = [&](const char *str, int x, int y, int scale, u8 r, u8 g, u8 b, bool shadow = true) {
		int cur_x = x;
		for (int i = 0; str[i]; i++) {
			int idx = font5x7_idx(str[i]);
			for (int row = 0; row < 7; row++) {
				for (int col = 0; col < 5; col++) {
					if (font5x7[idx][row][col] == '1') {
						if (shadow) {
							fill_rect(cur_x + col * scale + 1, y + row * scale + 2,
							          cur_x + (col + 1) * scale + 1, y + (row + 1) * scale + 2, 30, 45, 80);
						}
						fill_rect(cur_x + col * scale, y + row * scale,
						          cur_x + (col + 1) * scale, y + (row + 1) * scale, r, g, b);
					}
				}
			}
			cur_x += (5 + 1) * scale;
		}
	};

	// 4. Registration stickers
	// Month sticker (DEC) top left
	fill_rect(42, 12, 68, 28, 20, 50, 140);
	draw_string5x7("DEC", 45, 14, 1, 255, 255, 255, false);

	// Year sticker (05) top right
	fill_rect(W - 69, 12, W - 43, 28, 190, 30, 30);
	draw_string5x7("05", W - 63, 14, 1, 255, 230, 80, false);

	// 5. CALIFORNIA banner
	draw_string3x5("CALIFORNIA", 88, 14, 2, 200, 25, 25);

	// 6. Main plate number: MIDNIGHT
	draw_string5x7("MIDNIGHT", 32, 42, 4, 16, 32, 85, true);

	// 7. Bottom slogan: THE GOLDEN STATE
	draw_string5x7("THE GOLDEN STATE", 46, 104, 2, 70, 85, 115, false);

	s_PlateTex = gfxRegisterRgbaTexture("__licenseplate__", W, H, &rgba[0]);
	return s_PlateTex;
}

// Material from a vehicle shader template name (+ its first colour texture name).
static void ClassifyMaterial(rscViewerMatInfo &mat, const char *t, const char *tname, u32 carPaintColor)
{
	mat.color = mkrgba(90, 90, 95, 255);
	mat.alphaBlend = false;
	mat.texture = 0;
	SetMatShading(mat, 0.25f, 24.0f, 0.05f);
	if (strstr(t, "carpaint")) {
		mat.color = carPaintColor;
		SetMatShading(mat, 0.9f, 64.0f, 0.35f);
	} else if (strstr(t, "car_window")) {
		mat.color = mkrgba(28, 36, 46, 150);
		mat.alphaBlend = true;
		SetMatShading(mat, 1.2f, 96.0f, 0.6f, false, false, true);
	} else if (strstr(t, "colored_glass") || strstr(t, "glass")) {
		// light lens covers: taillight lenses ruby, everything else crystal
		if (strstr(tname, "tl") || strstr(tname, "tail") || strstr(tname, "brake") ||
		    strstr(t, "taillight") || strstr(t, "brakelight")) {
			mat.color = mkrgba(200, 20, 20, 110);
		} else {
			mat.color = mkrgba(235, 242, 255, 60);
		}
		mat.alphaBlend = true;
		SetMatShading(mat, 1.2f, 96.0f, 0.5f, false, false, true);
	} else if (strstr(t, "chrome")) {
		mat.color = mkrgba(240, 242, 245, 255);
		mat.texture = GetCarEnvMapTexture();
		SetMatShading(mat, 1.4f, 110.0f, 0.95f, true, false, false, true);
	} else if (strstr(t, "default_shiny")) {
		mat.color = mkrgba(60, 62, 66, 255);
		SetMatShading(mat, 0.6f, 40.0f, 0.15f);
	} else if (strstr(t, "black_matte") || strstr(t, "rubber")) {
		mat.color = mkrgba(30, 30, 33, 255);
		SetMatShading(mat, 0.08f, 8.0f, 0.02f);
	} else if (strstr(t, "carbon_fiber")) {
		mat.color = mkrgba(215, 215, 220, 255);     // modulates the weave texture
		SetMatShading(mat, 0.5f, 32.0f, 0.12f);
	} else if (strstr(t, "emissive_headlight") || strstr(t, "headlight") || strstr(t, "fog_light") || strstr(t, "foglight")) {
		mat.color = mkrgba(240, 245, 255, 255);
		SetMatShading(mat, 0.8f, 48.0f, 0.2f, false, true);
	} else if (strstr(t, "brakelight") || strstr(t, "taillight") || strstr(t, "thirdbrake")) {
		mat.color = mkrgba(215, 25, 25, 255);
		SetMatShading(mat, 0.8f, 48.0f, 0.2f, false, true);
	} else if (strstr(t, "reverselight")) {
		mat.color = mkrgba(240, 236, 220, 255);
		SetMatShading(mat, 0.8f, 48.0f, 0.2f, false, true);
	} else if (strstr(t, "licenseplate")) {
		mat.color = mkrgba(255, 255, 255, 255);
		mat.texture = GetLicensePlateTexture();
		SetMatShading(mat, 0.4f, 32.0f, 0.1f);
	} else if (strstr(t, "sprocket")) {
		// the bike chain sprocket is an alpha-cutout quad textured from the per-car trim
		// atlas (unavailable): invisible until a texture binds, not a black square
		mat.color = mkrgba(255, 255, 255, 0);
		mat.alphaBlend = true;
	} else if (strstr(t, "trim") || (tname && strstr(tname, "trim"))) {
		mat.color = mkrgba(38, 38, 42, 255);
		SetMatShading(mat, 0.35f, 24.0f, 0.08f);
	} else if (strstr(t, "decal") || (tname && strstr(tname, "decal"))) {
		// alpha-cutout images (decals, the bike chain sprocket) live in the per-car trim
		// atlas the viewer cannot load yet: invisible until a texture binds, not a black quad
		mat.color = mkrgba(255, 255, 255, 0);
		mat.alphaBlend = true;
	} else if (strstr(t, "drop_shadow")) {
		mat.color = mkrgba(255, 255, 255, 0);      // the viewer draws its own contact shadow
		mat.alphaBlend = true;
	} else if (strstr(t, "lit_textured")) {
		mat.color = mkrgba(255, 255, 255, 255);    // white modulate; replaced when no texture binds
		SetMatShading(mat, 0.35f, 24.0f, 0.08f);
	}
}

static inline float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// Sky gradient sampled by a direction: ground / horizon / sky.
static inline void EnvColor(const Vector3 &r, float &er, float &eg, float &eb)
{
	float y = r.y;
	if (y < 0.0f) {
		float t = Clamp01(-y);
		er = 0.55f + (0.16f - 0.55f) * t; eg = 0.58f + (0.16f - 0.58f) * t; eb = 0.62f + (0.18f - 0.62f) * t;
	} else {
		float t = Clamp01(y);
		er = 0.55f + (0.38f - 0.55f) * t; eg = 0.58f + (0.50f - 0.58f) * t; eb = 0.62f + (0.70f - 0.62f) * t;
	}
}

// Software vertex lighting: hemisphere ambient + key light diffuse + Blinn-Phong
// specular + fresnel clearcoat / environment reflection, times the pack's baked
// vertex colour as occlusion.  Returns a packed RGBA to hand to vglColor.
static inline u32 Shade(const Vector3 &p, const Vector3 &n, float ao, const rscViewerMatInfo &m)
{
	// mkrgba packs (a<<24)|(r<<16)|(g<<8)|b
	float br = ((m.color >> 16) & 0xff) / 255.0f, bg = ((m.color >> 8) & 0xff) / 255.0f, bb = (m.color & 0xff) / 255.0f;
	float alpha = ((m.color >> 24) & 0xff) / 255.0f;
	if (!s_Lit) return m.color;

	Vector3 V = s_CamPos - p;
	V.Normalize();
	float NdotL = n.Dot(s_LightDir); if (NdotL < 0.0f) NdotL = 0.0f;
	float NdotV = n.Dot(V); if (NdotV < 0.0f) NdotV = 0.0f;
	float hemi = 0.32f + 0.30f * (n.y * 0.5f + 0.5f);
	float fres = 1.0f - NdotV; fres *= fres; fres *= fres;
	Vector3 H = s_LightDir + V; H.Normalize();
	float NdotH = n.Dot(H); if (NdotH < 0.0f) NdotH = 0.0f;
	float spec = m.spec * powf(NdotH, m.gloss) * (NdotL > 0.0f ? 1.0f : 0.3f);
	Vector3 R = n * (2.0f * n.Dot(V)) - V;
	float er, eg, eb;
	EnvColor(R, er, eg, eb);

	float r, g, b;
	if (m.emissive) {
		float k = 0.8f + 0.2f * hemi;
		r = br * k; g = bg * k; b = bb * k;
	} else if (m.metallic) {
		if (m.isChrome && m.texture) {
			// Environment reflection is sampled from the bound envmap via hardware texgen reflection vector.
			// Vertex lighting modulates the reflection with key light diffuse + ambient.
			float diff = 0.70f + 0.30f * NdotL;
			r = br * diff * ao;
			g = bg * diff * ao;
			b = bb * diff * ao;
		} else {
			float k = m.reflect * (0.6f + 0.4f * ao);
			r = br * (er * k + hemi * 0.25f + 0.35f * NdotL);
			g = bg * (eg * k + hemi * 0.25f + 0.35f * NdotL);
			b = bb * (eb * k + hemi * 0.25f + 0.35f * NdotL);
		}
	} else {
		float k = (hemi + 0.85f * NdotL) * ao;
		float coat = m.reflect * (0.15f + 0.85f * fres);
		r = br * k + er * coat; g = bg * k + eg * coat; b = bb * k + eb * coat;
	}
	float sk = spec * (0.5f + 0.5f * ao);
	r += sk; g += sk; b += sk;
	if (m.glass) alpha = Clamp01(alpha + 0.45f * fres + 0.6f * spec);
	return mkrgba((u8)(Clamp01(r) * 255.0f), (u8)(Clamp01(g) * 255.0f), (u8)(Clamp01(b) * 255.0f), (u8)(alpha * 255.0f));
}

static inline void EmitBatchVertex(const rscGeomBatch &b, int i, bool colors, bool uvs, const Vector3 &n, const rscViewerMatInfo &m)
{
	float ao = 1.0f;
	if (m.vertexColor) {
		// baked lighting: pass the pack colour through (doubled, 0x80 = 1.0) with a touch of key light
		u32 c = colors ? b.colors[i] : 0x80808080u;
		int r = (int)(((c >> 16) & 0xff) * 2), g = (int)(((c >> 8) & 0xff) * 2), bl = (int)((c & 0xff) * 2);
		if (r > 255) r = 255; if (g > 255) g = 255; if (bl > 255) bl = 255;
		rscViewerMatInfo vm = m;
		vm.color = mkrgba((u8)r, (u8)g, (u8)bl, (u8)((m.color >> 24) & 0xff));
		vm.vertexColor = false;
		vglColor(Shade(b.verts[i], n, 1.0f, vm));
		vglNormal3f(n.x, n.y, n.z);
		if (uvs) vglTexCoord2f(b.uvs[i].x, b.uvs[i].y);
		if (m.layerScale[0] != 0.0f && uvs) vglTexCoord2f2(b.uvs[i].x * m.layerScale[0], b.uvs[i].y * m.layerScale[1]);
		else if (b.uvs2.GetCount() == b.verts.GetCount()) vglTexCoord2f2(b.uvs2[i].x, b.uvs2[i].y);
		vglVertex3f(b.verts[i]);
		return;
	}
	if (colors) {
		u32 c = b.colors[i];
		// 0x80 = 1.0 in the pack; use the baked colour's luminance as occlusion
		float l = (((c >> 16) & 0xff) * 0.3f + ((c >> 8) & 0xff) * 0.59f + (c & 0xff) * 0.11f) / 128.0f;
		ao = 0.55f + 0.45f * Clamp01(l);      // soft: some packs bake uneven per-vertex colours
	}
	vglColor(Shade(b.verts[i], n, ao, m));
	vglNormal3f(n.x, n.y, n.z);
	if (uvs) vglTexCoord2f(b.uvs[i].x, b.uvs[i].y);
	vglVertex3f(b.verts[i]);
}

static void DrawBatch(const rscGeomBatch &b, const rscViewerMatInfo &m)
{
	int n = b.verts.GetCount();
	if (n < 3) return;
	bool colors = b.colors.GetCount() == n;
	bool uvs = b.uvs.GetCount() == n;
	bool normals = b.normals.GetCount() == n;

	atArray<rscTriangle> tris;
	b.BuildTriangles(tris, s_Prim);
	if (!tris.GetCount()) return;

	if (s_Wire) {
		vglBegin(drawLine, tris.GetCount() * 6);
		for (int i = 0; i < tris.GetCount(); i++) {
			const rscTriangle &t = tris[i];
			const Vector3 &v0 = b.verts[t.i0];
			const Vector3 &v1 = b.verts[t.i1];
			const Vector3 &v2 = b.verts[t.i2];
			vglColor(mkrgba(255, 255, 255, 255));
			vglVertex3f(v0); vglVertex3f(v1);
			vglVertex3f(v1); vglVertex3f(v2);
			vglVertex3f(v2); vglVertex3f(v0);
		}
		vglEnd();
		return;
	}

	vglBegin(drawTriangles, tris.GetCount() * 3);
	for (int i = 0; i < tris.GetCount(); i++) {
		const rscTriangle &t = tris[i];
		if (normals) {
			EmitBatchVertex(b, t.i0, colors, uvs, b.normals[t.i0], m);
			EmitBatchVertex(b, t.i1, colors, uvs, b.normals[t.i1], m);
			EmitBatchVertex(b, t.i2, colors, uvs, b.normals[t.i2], m);
		} else {
			// no normals in the stream: flat-shade with the face normal, pointed away from the car centre
			Vector3 e1 = b.verts[t.i1] - b.verts[t.i0], e2 = b.verts[t.i2] - b.verts[t.i0];
			Vector3 fn; fn.Cross(e1, e2);
			if (fn.MagSq() < 1e-12f) fn.Set(0.0f, 1.0f, 0.0f); else fn.Normalize();
			Vector3 c = (b.verts[t.i0] + b.verts[t.i1] + b.verts[t.i2]) * (1.0f / 3.0f);
			if (fn.Dot(c - s_Center) < 0.0f) fn.Negate();
			EmitBatchVertex(b, t.i0, colors, uvs, fn, m);
			EmitBatchVertex(b, t.i1, colors, uvs, fn, m);
			EmitBatchVertex(b, t.i2, colors, uvs, fn, m);
		}
	}
	vglEnd();
}

static atArray<rscTexInfo> s_CityShaders;
static atArray<gfxTexture *> s_CityTextures;   // per shader index, lazily loaded (NULL = none)
static atArray<u8> s_CityTexTried;
static const datResourceImage *s_Image = 0;
static const char *s_PackName = 0;
static bool s_Textured = false;

// -ppf pages: the drawable's shader table instead (page textures, slot 1)
static atArray<rscShaderInfo> s_PageShaders;

static gfxTexture *CityTextureFor(int shaderIdx)
{
	int count = s_PageShaders.GetCount() ? s_PageShaders.GetCount() : s_CityShaders.GetCount();
	if (!s_Textured || shaderIdx < 0 || shaderIdx >= count) return 0;
	if (!s_CityTexTried[shaderIdx]) {
		s_CityTexTried[shaderIdx] = 1;
		char name[64];
		if (s_PageShaders.GetCount()) {
			const rscShaderInfo &sh = s_PageShaders[shaderIdx];
			formatf(name, sizeof(name), "rscview_page_%d", shaderIdx);
			for (int t = 0; t < sh.numPageTextures && !s_CityTextures[shaderIdx]; t++)
				s_CityTextures[shaderIdx] = rscLoadPageTexture(1, sh.pageTextures[t], name);
		} else {
			formatf(name, sizeof(name), "rscview_city_%d", shaderIdx);
			s_CityTextures[shaderIdx] = rscLoadCityTexture(*s_Image, 0, s_CityShaders[shaderIdx], name);
		}
	}
	return s_CityTextures[shaderIdx];
}

// The skyhat's models index the city root's sky shader group (+0x10), not the city groups.
static atArray<rscTexInfo> s_SkyShaders;
static atArray<gfxTexture *> s_SkyTextures;
static atArray<u8> s_SkyTexTried;
static atArray<u32> s_SkyModels;
// City model address -> the draw pass its model type files it under (0 MAIN, 1 GROUND,
// 2 REFLECT, 3 HDR, 4 ALPHA; type +0x3c + (lod * 5 + part) * 4), filled by rscScanCityModelNames.
static std::unordered_map<u32, int> s_CityModelPart;

static bool IsSkyModel(u32 addr)
{
	for (int i = 0; i < s_SkyModels.GetCount(); i++)
		if (s_SkyModels[i] == addr) return true;
	return false;
}

static gfxTexture *SkyTextureFor(int shaderIdx)
{
	if (!s_Image || shaderIdx < 0 || shaderIdx >= s_SkyShaders.GetCount()) return 0;
	if (!s_SkyTexTried[shaderIdx]) {
		s_SkyTexTried[shaderIdx] = 1;
		char name[64];
		formatf(name, sizeof(name), "rscview_sky_%d", shaderIdx);
		s_SkyTextures[shaderIdx] = rscLoadCityTexture(*s_Image, -1, s_SkyShaders[shaderIdx], name);
	}
	return s_SkyTextures[shaderIdx];
}

// mcShaderCityWindow (city_window template): the window layer's texture with the pass's
// GS colour modulation baked in (RGB * WindowTint / 128).  The base pass writes its alpha
// (the window mask) and the layer blends "Cs 0 Ad Cd" - stage-2 combine 2 adds it weighted
// by the base texture's alpha.
static atArray<gfxTexture *> s_WinTextures;
static atArray<u8> s_WinTexTried;

static gfxTexture *CityWindowTextureFor(int shaderIdx, bool &outReflect)
{
	outReflect = false;
	if (!s_Image || !s_Textured || shaderIdx < 0 || shaderIdx >= s_CityShaders.GetCount()) return 0;
	const rscTexInfo &sh = s_CityShaders[shaderIdx];
	if (sh.shaderType != 16 || sh.templateSlot != 1 || ARGS.Get("nowindows")) return 0;
	bool isDaytime = (s_PackName && !strstr(s_PackName, "midnight"));
	if (isDaytime) {
		outReflect = true;
		static bool sLogged = false;
		if (!sLogged) { Displayf("rscview: daytime window reflection layer active (fx_car_viewer_envmap, shader %d)", shaderIdx); sLogged = true; }
		return gfxGetTexture("fx_car_viewer_envmap", true, false);
	}
	if (!sh.layerTex) return 0;
	if (s_WinTextures.GetCount() != s_CityShaders.GetCount()) {
		s_WinTextures.Resize(s_CityShaders.GetCount());
		s_WinTexTried.Resize(s_CityShaders.GetCount());
		for (int i = 0; i < s_WinTextures.GetCount(); i++) { s_WinTextures[i] = 0; s_WinTexTried[i] = 0; }
	}
	if (!s_WinTexTried[shaderIdx]) {
		s_WinTexTried[shaderIdx] = 1;
		rscTexInfo layer;
		char name[64];
		formatf(name, sizeof(name), "rscview_win_%d", shaderIdx);
		if (rscParseTexInfo(*s_Image, sh.layerTex, layer))
			s_WinTextures[shaderIdx] = rscLoadCityTexture(*s_Image, 0, layer, name, sh.tint, 128);
	}
	return s_WinTextures[shaderIdx];
}

// city_road (road instance, class 17): texture %1 is a shared grime / detail layer (black with
// alpha) at scales/scalet (4x the base UVs), blended "normal" over the base with the vertex
// colour kept - vglTex2Combine(3).  -nodetail leaves it off.
static std::unordered_map<u32, gfxTexture *> s_RoadDetail;

static gfxTexture *CityRoadDetailFor(int shaderIdx)
{
	if (!s_Image || !s_Textured || shaderIdx < 0 || shaderIdx >= s_CityShaders.GetCount() || ARGS.Get("nodetail")) return 0;
	const rscTexInfo &sh = s_CityShaders[shaderIdx];
	if (sh.shaderType != 17 || !sh.layerTex) return 0;
	// the road shaders' wrapper objects share a few resident blocks: cache by the block
	rscTexInfo detail;
	if (!rscParseTexInfo(*s_Image, sh.layerTex, detail)) return 0;
	const u32 key = detail.residentAddr ? detail.residentAddr : sh.layerTex;
	std::unordered_map<u32, gfxTexture *>::iterator it = s_RoadDetail.find(key);
	if (it != s_RoadDetail.end()) return it->second;
	float detailScale = 1.0f;                    // -citydetailscale <f>: blend weight (as in the game)
	ARGS.Get("citydetailscale", 1.0f, detailScale);
	char name[64];
	formatf(name, sizeof(name), "rscview_roaddetail_%08x", key);
	gfxTexture *tex = detailScale > 0.0f ? rscLoadCityTexture(*s_Image, 0, detail, name, NULL, (int)(128.0f * detailScale + 0.5f)) : 0;
	s_RoadDetail[key] = tex;
	return tex;
}

// Effective material of one geometry (per-part overrides on top of the shader material).

// Which vehicle parts make up the stock car.  Two naming schemes exist:
//  * customisable cars: a family stem + "_stk" is the stock piece and a stem with a
//    nonzero number is a kit variant ("bumf11_", "splr3_", "hd7_", "shd2_", "chptp1_",
//    "tl1_elgte_01", SSR-style "shd_kit_01_");
//  * plain cars: no "_stk" at all ("hd_hd00", "bmpf_bumf", "hoodbone_hd", "hood_hood",
//    "kit00_hd_Hood", "kit_stock_hood_0", "lodGroup1_Body_H") - index 0 / "stock" is
//    the stock piece.
// So: stock tokens are accepted, nonzero-numbered variants rejected, and everything
// else (shell group, plain hoods, suspension arms, badges) is part of the stock car.
static bool sIsLodOrEffect(const char *n)
{
	// (lower LODs live in the other LOD tables; "_l.mesh" is a Suspension_Arm_Front_L, not a LOD)
	return strstr(n, "mlod") || strstr(n, "llod") || strstr(n, "lod_m") || strstr(n, "lod_l") || strstr(n, "neonglow");
}

static bool sHasStockToken(const char *n)
{
	for (const char *p = n; (p = strstr(p, "_stk")) != 0; p++) {
		if (p[4] == '_' || p[4] == '.' || p[4] == 0) return true;
	}
	return false;
}

// "<letters><digits>_" / "<letters><digits>." with a nonzero number, or "_kit_<digits>_"
// with a nonzero number.  "lodGroup2_" is a LOD group, not a variant.
static bool sIsNumberedVariant(const char *n)
{
	// only tokens before the first "_stk" count: "lss_stk_cl5_..." carries a car code after it
	const char *stop = strstr(n, "_stk");
	for (const char *p = n; *p && (!stop || p < stop); p++) {
		if (p != n && p[-1] != '_') continue;
		const char *q = p;
		while (*q >= 'a' && *q <= 'z') q++;
		size_t letters = (size_t)(q - p);
		if (letters < 2 || *q < '0' || *q > '9') continue;
		int num = 0;
		const char *r = q;
		while (*r >= '0' && *r <= '9') { num = num * 10 + (*r - '0'); r++; }
		if (*r != '_' && *r != '.' && *r != 0) continue;
		if (r - q > 2) continue;                       // long numbers are part codes, not indices
		if (!strncmp(p, "lodgroup", 8) || !strncmp(p, "lod", letters)) continue;
		if (num != 0 && strncmp(r, "_stk", 4) != 0) return true;
	}
	for (const char *p = n; (p = strstr(p, "_kit_")) != 0; p++) {
		const char *q = p + 5;
		int num = 0;
		bool digits = false;
		while (*q >= '0' && *q <= '9') { num = num * 10 + (*q - '0'); q++; digits = true; }
		if (digits && (*q == '_' || *q == '.' || *q == 0) && num != 0) return true;
	}
	return false;
}

static bool sIsStockCarPart(const char *n)
{
	if (sIsLodOrEffect(n)) return false;
	// the shell group is never a kit variant, whatever indices its pieces carry (headlights1, hlight_lense2)
	if (strstr(n, "hlod") || strstr(n, "shell_lod") || strstr(n, "shellgroup")) return true;
	if (sIsNumberedVariant(n)) return false;   // before the stock-token test: "onesht1_..._latch_stk" is kit 1
	if (sHasStockToken(n)) return true;
	if (strstr(n, "_stk")) return false;       // "_stksmkd" and the like: a named variant of a stock piece
	return true;
}

static bool ResolveMaterial(const rscModel &model, const rscGeometry &geom, rscViewerMatInfo &out)
{
	out.color = 0; out.alphaBlend = false; out.texture = 0; out.invAlpha = false; out.alphaRef = 0; out.layer = 0; out.layerCombine = 2; out.layerScale[0] = out.layerScale[1] = 0.0f; out.layerReflect = false;
	SetMatShading(out, 0.25f, 24.0f, 0.05f);
	// Car assembly tags rim/brake parts with 1000 + material; city packs use real shader
	// indices up to their slot count (1516 in sd), so only take this path when rims exist.
	if (geom.shaderIdx >= 1000 && s_RimMaterials.GetCount() > 0) {
		int rimIdx = geom.shaderIdx - 1000;
		if (rimIdx < 0 || rimIdx >= s_RimMaterials.GetCount()) return false;
		out = s_RimMaterials[rimIdx];
		return true;
	}
	if (s_IsCarAssemble && s_CarMaterials.GetCount() > 0) {
		if (geom.shaderIdx < 0 || geom.shaderIdx >= s_CarMaterials.GetCount()) return false;
		out = s_CarMaterials[geom.shaderIdx];
		const char *tmpl = geom.shaderIdx < carShaders.GetCount() ? carShaders[geom.shaderIdx].templateName : "";
		if (model.isTaillight && (strstr(tmpl, "colored_glass") || strstr(tmpl, "glass"))) {
			out.color = mkrgba(215, 20, 20, 150);
			out.alphaBlend = true;
			SetMatShading(out, 1.2f, 96.0f, 0.5f, false, false, true);
		} else if (model.isHeadlight) {
			if (strstr(tmpl, "colored_glass") || strstr(tmpl, "glass")) {
				out.color = mkrgba(235, 242, 255, 60);
				out.alphaBlend = true;
				SetMatShading(out, 1.2f, 96.0f, 0.5f, false, false, true);
			} else if (strstr(tmpl, "player_taillight") || strstr(tmpl, "player_brakelight")) {
				// turn-signal / marker lens inside the headlight cluster: amber, not solid red
				out.color = mkrgba(250, 160, 40, 170);
				out.alphaBlend = true;
				SetMatShading(out, 1.0f, 64.0f, 0.3f, false, false, true);
			}
		}
		if (strstr(tmpl, "player_") && !model.isHeadlight && !out.alphaBlend) {
			// lens covers on the tail cluster: translucent so the reflector shows through
			out.color = (out.color & 0x00ffffff) | (170u << 24);
			out.alphaBlend = true;
			SetMatShading(out, 1.0f, 64.0f, 0.3f, false, false, true);
		}
		return true;
	}
	if (IsSkyModel(model.addr)) {
		// mcSkyHatClass draws with rmcbsOverwrite and alpha test >= 0: opaque, texture alpha ignored
		out.texture = SkyTextureFor(geom.shaderIdx);
		out.alphaBlend = false;
	} else {
		out.texture = CityTextureFor(geom.shaderIdx);
		int state = -1;
		if (!s_PageShaders.GetCount() && geom.shaderIdx >= 0 && geom.shaderIdx < s_CityShaders.GetCount() && !ARGS.Get("texalphablend")) {
			const rscTexInfo &sh = s_CityShaders[geom.shaderIdx];
			// the draw part belongs to the whole model (its type's LOD/part table), not to the
			// geometry list index; a model no type references draws with the MAIN pass
			std::unordered_map<u32, int>::const_iterator pit = s_CityModelPart.find(model.addr);
			state = rscCityBlendState(pit != s_CityModelPart.end() ? pit->second : 0, sh.shaderType, sh.templateSlot);
		}
		if (state < 0) {
			// page-file drawables and unknown shader classes (or -texalphablend): the texture's alpha decides
			out.alphaBlend = (out.texture && out.texture->HasAlpha());
		} else {
			out.alphaBlend = state != rscCityOpaque;
			out.invAlpha = state == rscCityInvBlend;
			out.alphaRef = state == rscCityBlend45 ? 90 : 0;
		}
		if (!s_PageShaders.GetCount())
			out.layer = CityWindowTextureFor(geom.shaderIdx, out.layerReflect);
		if (!out.layer && !s_PageShaders.GetCount() && (out.layer = CityRoadDetailFor(geom.shaderIdx)) != 0) {
			out.layerCombine = 3;
			out.layerScale[0] = s_CityShaders[geom.shaderIdx].layerScale[0];
			out.layerScale[1] = s_CityShaders[geom.shaderIdx].layerScale[1];
		}
	}
	out.color = mkrgba(255, 255, 255, 255);
	SetMatShading(out, 0.15f, 12.0f, 0.04f, false, true);   // baked vertex lighting, near-emissive
	out.vertexColor = true;
	return true;
}

struct rscDrawItem { int model, geom; float dist; };

static void DrawGround()
{
	// A soft disc under the car at the lowest point of the assembly, then a
	// multiplied contact shadow the size of the footprint.
	float y = s_Min.y - 0.005f;
	float rad = (s_Max - s_Min).Mag() * 2.2f;
	const int N = 48;
	u32 clear = ageClearColor;
	u8 cr = (u8)((clear >> 16) & 0xff), cg = (u8)((clear >> 8) & 0xff), cb = (u8)(clear & 0xff);
	RSTATE.SetTexture(0);
	RSTATE.SetAlphaBlendEnable(false);
	RSTATE.SetZWriteEnable(true);
	vglBegin(drawTriangles, N * 3);
	for (int i = 0; i < N; i++) {
		float a0 = (float)i / N * 6.2831853f, a1 = (float)(i + 1) / N * 6.2831853f;
		vglColor(mkrgba(cr + 26, cg + 26, cb + 26, 255));
		vglVertex3f(s_Center.x, y, s_Center.z);
		vglColor(mkrgba(cr, cg, cb, 255));
		vglVertex3f(s_Center.x + cosf(a0) * rad, y, s_Center.z + sinf(a0) * rad);
		vglVertex3f(s_Center.x + cosf(a1) * rad, y, s_Center.z + sinf(a1) * rad);
	}
	vglEnd();

	float ex = (s_Max.x - s_Min.x) * 0.5f * 1.02f, ez = (s_Max.z - s_Min.z) * 0.5f * 1.02f;
	float sy = y + 0.003f;
	RSTATE.SetZWriteEnable(false);
	RSTATE.SetAlphaBlendEnable(true);
	RSTATE.SetSrcBlend(blendZero);
	RSTATE.SetDestBlend(blendSrcColor);
	const int RINGS = 3;
	float ringR[RINGS + 1] = {0.0f, 0.55f, 0.85f, 1.25f};
	u8 ringC[RINGS + 1] = {90, 120, 190, 255};        // multiplier: 90 = deep shadow, 255 = none
	vglBegin(drawTriangles, RINGS * N * 6);
	for (int rI = 0; rI < RINGS; rI++) {
		float r0 = ringR[rI], r1 = ringR[rI + 1];
		u8 c0 = ringC[rI], c1 = ringC[rI + 1];
		for (int i = 0; i < N; i++) {
			float a0 = (float)i / N * 6.2831853f, a1 = (float)(i + 1) / N * 6.2831853f;
			float x00 = s_Center.x + cosf(a0) * ex * r0, z00 = s_Center.z + sinf(a0) * ez * r0;
			float x01 = s_Center.x + cosf(a1) * ex * r0, z01 = s_Center.z + sinf(a1) * ez * r0;
			float x10 = s_Center.x + cosf(a0) * ex * r1, z10 = s_Center.z + sinf(a0) * ez * r1;
			float x11 = s_Center.x + cosf(a1) * ex * r1, z11 = s_Center.z + sinf(a1) * ez * r1;
			vglColor(mkrgba(c0, c0, c0, 255)); vglVertex3f(x00, sy, z00);
			vglColor(mkrgba(c1, c1, c1, 255)); vglVertex3f(x10, sy, z10);
			vglColor(mkrgba(c1, c1, c1, 255)); vglVertex3f(x11, sy, z11);
			vglColor(mkrgba(c0, c0, c0, 255)); vglVertex3f(x00, sy, z00);
			vglColor(mkrgba(c1, c1, c1, 255)); vglVertex3f(x11, sy, z11);
			vglColor(mkrgba(c0, c0, c0, 255)); vglVertex3f(x01, sy, z01);
		}
	}
	vglEnd();
	RSTATE.SetSrcBlend(blendSrcAlpha);
	RSTATE.SetDestBlend(blendInvSrcAlpha);
	RSTATE.SetAlphaBlendEnable(false);
	RSTATE.SetZWriteEnable(true);
}

static void DrawModels()
{
	if (s_IsCarAssemble && !s_Wire && !ARGS.Get("noground")) DrawGround();

	// Pass 0: opaque geometry.  Pass 1: blended geometry (glass, lenses, rim faces),
	// sorted back to front so the far side of the cabin draws before the near glass.
	static atArray<rscDrawItem> blended;
	blended.Reset();
	static bool sTexLogDone = false;
	bool texlog = !sTexLogDone && ARGS.Get("texlog");

	// PS2 opaque passes test nothing: a texel's alpha must not punch holes there
	const int callerAlphaFunc = RSTATE.GetAlphaFunc(), callerAlphaRef = RSTATE.GetAlphaRef();
	RSTATE.SetAlphaFunc(alphaAlways);
	// opaque pass: the renderer must not turn an all-translucent texture (city sidewalk /
	// grass / asphalt: alpha is a wet-reflection mask) into a blended, depthless draw
	RSTATE.SetTextureAlphaBlendAllowed(false);
	RSTATE.SetZWriteEnable(true);
	for (int m = 0; m < s_Models.GetCount(); m++) {
		const rscModel &model = s_Models[m];
		for (int g = 0; g < model.geometries.GetCount(); g++) {
			const rscGeometry &geom = model.geometries[g];
			if (s_Part >= 0 && geom.part != s_Part) continue;
			rscViewerMatInfo mat;
			if (!ResolveMaterial(model, geom, mat)) continue;
			if (mat.alphaBlend && !mat.texture && ((mat.color >> 24) & 0xff) == 0) continue;
			if (texlog)
				Displayf("rscview: model %d geometry %d shader %d -> texture %s (%dx%d), %d batches, uvs %s, normals %s%s", m, g, geom.shaderIdx,
				         mat.texture ? mat.texture->GetName() : "none", mat.texture ? mat.texture->GetWidth() : 0, mat.texture ? mat.texture->GetHeight() : 0, geom.batches.GetCount(),
				         geom.batches.GetCount() && geom.batches[0].uvs.GetCount() == geom.batches[0].verts.GetCount() ? "yes" : "NO",
				         geom.batches.GetCount() && geom.batches[0].normals.GetCount() == geom.batches[0].verts.GetCount() ? "yes" : "NO",
				         mat.alphaBlend ? (mat.invAlpha ? " [inv-blend]" : mat.alphaRef ? " [blend, alpha>90]" : " [blend]") : "");
			if (mat.alphaBlend) {
				rscDrawItem it;
				it.model = m; it.geom = g;
				Vector3 c = (geom.batches.GetCount() && geom.batches[0].verts.GetCount()) ? geom.batches[0].verts[0] : s_Center;
				it.dist = (c - s_CamPos).MagSq();
				blended.Append(it);
				continue;
			}
			bool isChrome = mat.isChrome && mat.texture;
			if (isChrome) {
				RSTATE.SetTexGeneration(0, 0, false, texsrcCameraSpaceReflectionVector, 0);
			}
			RSTATE.SetTexture(mat.texture);
			if (mat.layer) {
				if (mat.layerReflect) RSTATE.SetTexGeneration(1, 0, false, texsrcCameraSpaceReflectionVector, 0);
				vglBindTexture2(mat.layer);
				vglTex2Combine(mat.layerCombine);
			}   // city_window layer (2) / city_road detail (3)
			for (int b = 0; b < geom.batches.GetCount(); b++)
				DrawBatch(geom.batches[b], mat);
			if (mat.layer) {
				if (mat.layerReflect) RSTATE.SetTexGeneration(1, 0, false, 0, 0);
				vglTex2Combine(0);
				vglBindTexture2(NULL);
			}
			if (mat.texture) RSTATE.SetTexture(0);
			if (isChrome) {
				RSTATE.SetTexGeneration(0, 0, false, 0, 0);
			}
		}
	}
	sTexLogDone = true;

	// simple insertion sort, far first
	for (int i = 1; i < blended.GetCount(); i++) {
		rscDrawItem it = blended[i];
		int j = i - 1;
		while (j >= 0 && blended[j].dist < it.dist) { blended[j + 1] = blended[j]; j--; }
		blended[j + 1] = it;
	}
	RSTATE.SetTextureAlphaBlendAllowed(true);
	RSTATE.SetZWriteEnable(false);
	RSTATE.SetAlphaBlendEnable(true);
	for (int i = 0; i < blended.GetCount(); i++) {
		const rscModel &model = s_Models[blended[i].model];
		const rscGeometry &geom = model.geometries[blended[i].geom];
		rscViewerMatInfo mat;
		ResolveMaterial(model, geom, mat);
		bool isChrome = mat.isChrome && mat.texture;
		if (isChrome) {
			RSTATE.SetTexGeneration(0, 0, false, texsrcCameraSpaceReflectionVector, 0);
		}
		// city shaders: rmcbsNormal with alpha > 45 (PS2 scale), or the cutout template's inverted blend
		RSTATE.SetBlendSet(mat.invAlpha ? blendSet_InvSrcAlpha_SrcAlpha : blendSet_SrcAlpha_InvSrcAlpha);
		RSTATE.SetAlphaFunc(mat.alphaRef > 0 ? alphaGreater : alphaAlways);
		if (mat.alphaRef > 0) RSTATE.SetAlphaRef(mat.alphaRef);
		RSTATE.SetTexture(mat.texture);
		if (mat.layer) {
			if (mat.layerReflect) RSTATE.SetTexGeneration(1, 0, false, texsrcCameraSpaceReflectionVector, 0);
			vglBindTexture2(mat.layer);
			vglTex2Combine(mat.layerCombine);
		}
		for (int b = 0; b < geom.batches.GetCount(); b++)
			DrawBatch(geom.batches[b], mat);
		if (mat.layer) {
			if (mat.layerReflect) RSTATE.SetTexGeneration(1, 0, false, 0, 0);
			vglTex2Combine(0);
			vglBindTexture2(NULL);
		}
		if (mat.texture) RSTATE.SetTexture(0);
		if (isChrome) {
			RSTATE.SetTexGeneration(0, 0, false, 0, 0);
		}
	}
	RSTATE.SetBlendSet(blendSet_SrcAlpha_InvSrcAlpha);
	RSTATE.SetAlphaFunc(callerAlphaFunc);
	RSTATE.SetAlphaRef(callerAlphaRef);
	RSTATE.SetAlphaBlendEnable(false);
	RSTATE.SetZWriteEnable(true);
}

//-----------------------------------------------------------------------------
// -ambient <vehicle>: one MC3 ambient (traffic) vehicle, loaded and drawn the way
// the game's traffic does it.  aiAmbientType::LoadAndInitialization loads a
// drwShaderModel out of vehicle/<name>/<name>.type and builds a crSkeleton over
// its .skel; aiAmbientVehicle::Render (dtMAIN_UNSHADOWED) rolls the wheel bones in
// that bone palette and draws the model through it.  The city traffic packs hold
// the same vehicles, but every one of them is also on disc loose, in the text
// formats the PC loader reads, so this runs the real load and draw path rather
// than a pack decoder.
//
// The port does not consume the ambientBodyColor shader locals, so -bodycolor N
// tints the ambient_body materials through their diffuse instead, with the N'th
// BodyColorTune entry from tune/traffic/<name>.vehicle.
//-----------------------------------------------------------------------------

static const int kMaxAmbientBones = 128;
static const int kMaxAmbientWheelsPerSide = 8;

struct rscAmbientShader {
	char templ[64];
	char tex[64];
};

// The .type's shading group in declaration order: a mesh material named "#<n>"
// is the n'th entry.
static int sReadAmbientShaders(const char *name, atArray<rscAmbientShader> &out)
{
	out.Reset();
	Stream *s = ASSET.Open(name, "type");
	if (!s)
		return 0;
	datTokenizer tok;
	tok.Init(name, s);
	char t[128];
	for (;;) {
		t[0] = 0;
		if (!tok.GetToken(t, sizeof(t)) || !t[0])
			break;
		if (_stricmp(t, "Shaders"))
			continue;
		int count = tok.GetInt();
		tok.GetToken(t, sizeof(t));                      // {
		for (int i = 0; i < count; i++) {
			rscAmbientShader sh;
			sh.templ[0] = 0;
			sh.tex[0] = 0;
			tok.GetToken(sh.templ, sizeof(sh.templ));
			int numTex = tok.GetInt();
			for (int k = 0; k < numTex; k++) {
				tok.GetToken(t, sizeof(t));
				if (k == 0)
					formatf(sh.tex, sizeof(sh.tex), "%s", t);
			}
			out.Append(sh);
		}
		break;
	}
	s->Close();
	return out.GetCount();
}

// BodyColorTune in tune/traffic/<name>.vehicle: the RGB triples an instance's
// paint is picked from (aiAmbientType::SetAmbientBodyColor).
static int sReadAmbientBodyColors(const char *name, atArray<Vector3> &out)
{
	out.Reset();
	char path[128];
	formatf(path, sizeof(path), "$/tune/traffic/%s", name);
	Stream *s = ASSET.Open(path, "vehicle");
	if (!s)
		return 0;
	datTokenizer tok;
	tok.Init(path, s);
	char t[64];
	for (;;) {
		t[0] = 0;
		if (!tok.GetToken(t, sizeof(t)) || !t[0])
			break;
		if (_stricmp(t, "BodyColorTune"))
			continue;
		for (;;) {
			float rgb[3];
			int got = 0;
			while (got < 3) {
				t[0] = 0;
				if (!tok.GetToken(t, sizeof(t)) || !t[0])
					break;
				char *end = 0;
				rgb[got] = (float)strtod(t, &end);
				if (end == t)
					break;
				got++;
			}
			if (got < 3)
				break;
			out.Append(Vector3(rgb[0], rgb[1], rgb[2]));
		}
		break;
	}
	s->Close();
	return out.GetCount();
}

// aiAmbientType::GetIndexForTag: a .type bonetag name to its bone index.
static int sAmbientTag(drwShaderModel &model, const char *tag)
{
	drwBonyData *bony = model.GetBonyData();
	if (!bony || !bony->m_pTagHash || !tag)
		return -1;
	void *data = bony->m_pTagHash->Access(tag);
	return data ? (int)(intptr_t)data - 1 : -1;
}

static int RunAmbientViewer(const char *nameArg)
{
	// A va_ folder name, or the same name without its prefix.
	char name[64], folder[96], probe[160];
	formatf(name, sizeof(name), "%s", nameArg);
	formatf(probe, sizeof(probe), "vehicle/%s/%s", name, name);
	if (!ASSET.Exists(probe, "type") && _strnicmp(name, "va_", 3)) {
		formatf(name, sizeof(name), "va_%s", nameArg);
		formatf(probe, sizeof(probe), "vehicle/%s/%s", name, name);
	}
	if (!ASSET.Exists(probe, "type")) {
		Errorf("rscview: no ambient vehicle '%s' (no %s.type; the ambient vehicles are the va_* folders under vehicle/)", nameArg, probe);
		return 1;
	}
	formatf(folder, sizeof(folder), "vehicle/%s", name);

	// Same order as aiAmbientType::LoadAndInitialization.  The folder stays pushed:
	// the model's textures resolve through its texture/ subfolder.
	ASSET.PushFolder(folder);
	drwShaderModel *model = new drwShaderModel;
	model->PreLoadBonetags(name);
	if (!model->Load(name)) {
		Errorf("rscview: failed to load ambient model '%s'", name);
		return 1;
	}
	crSkeletonData *skelData = model->GetSkeletonData();
	gfxModel *high = model->GetModel(0);
	if (!skelData || skelData->GetNumBones() <= 0 || !high) {
		Errorf("rscview: '%s' loaded without %s", name, high ? "a skeleton" : "a high LOD");
		return 1;
	}
	const int numBones = skelData->GetNumBones();
	if (numBones > kMaxAmbientBones) {
		Errorf("rscview: '%s' has %d bones, more than the %d this viewer allows", name, numBones, kMaxAmbientBones);
		return 1;
	}
	rmcShaderData *locals = (model->GetShaderGroupCount() > 0) ? model->GetShaderGroup(0).AllocateLocals() : 0;

	static Matrix34 sRoot;
	sRoot.Identity();
	crSkeleton *skel = new crSkeleton(numBones);
	skel->Init(*skelData, &sRoot);
	skel->Update();
	static Matrix34 sPalette[kMaxAmbientBones];
	for (int i = 0; i < numBones; i++)
		sPalette[i] = skel->GetGlobalMtx(i);

	// Wheels and the drop shadow, by bone tag, as aiAmbientType finds them.
	int wheelBones[kMaxAmbientWheelsPerSide * 2];
	int numWheels = 0;
	for (int i = 0; i < kMaxAmbientWheelsPerSide; i++) {
		char tag[16];
		formatf(tag, sizeof(tag), "whl_l%d", i);
		int l = sAmbientTag(*model, tag);
		if (l >= 0 && l < numBones) wheelBones[numWheels++] = l;
		formatf(tag, sizeof(tag), "whl_r%d", i);
		int r = sAmbientTag(*model, tag);
		if (r >= 0 && r < numBones) wheelBones[numWheels++] = r;
	}
	const int shadowBone = sAmbientTag(*model, "drop_shadow");

	atBitSet enables;
	enables.Init(kMaxAmbientBones);
	enables.SetAll();
	if (ARGS.Get("noshadow") && shadowBone >= 0)
		enables.Set(shadowBone, false);
	if (ARGS.Get("nowheels"))
		for (int w = 0; w < numWheels; w++)
			enables.Set(wheelBones[w], false);

	atArray<rscAmbientShader> shaders;
	sReadAmbientShaders(name, shaders);
	atArray<Vector3> bodyColors;
	sReadAmbientBodyColors(name, bodyColors);

	int bodyColor = -1;
	ARGS.Get("bodycolor", -1, bodyColor);
	if (bodyColor >= 0 && bodyColors.GetCount() > 0) {
		const Vector3 &c = bodyColors[bodyColor % bodyColors.GetCount()];
		int tinted = 0;
		for (int m = 0; m < high->materials.GetCount(); m++) {
			gfxModelMaterial &mat = high->materials[m];
			if (mat.name.size() < 2 || mat.name[0] != '#')
				continue;
			int si = atoi(mat.name.c_str() + 1);
			if (si < 0 || si >= shaders.GetCount() || !strstr(shaders[si].templ, "ambient_body"))
				continue;
			mat.diffuse[0] = c.x;
			mat.diffuse[1] = c.y;
			mat.diffuse[2] = c.z;
			tinted++;
		}
		Displayf("rscview: body colour %d of %d (%.2f %.2f %.2f) on %d ambient_body materials",
		         bodyColor % bodyColors.GetCount(), bodyColors.GetCount(), c.x, c.y, c.z, tinted);
	} else if (bodyColor >= 0) {
		Warningf("rscview: no BodyColorTune for '%s' in tune/traffic/%s.vehicle", name, name);
	}

	// Debug switches for judging what the loader built (all viewer-side):
	//   -hidemtl <n>       drop the packets drawn with material n (numbers from -list)
	//   -nocpv             ignore the mesh vertex colours (neutral 0x80 grey instead)
	//   -cpvalpha <0-255>  force the alpha of every vertex colour
	//   -notex             draw the materials untextured
	int hideMtl = -1;
	ARGS.Get("hidemtl", -1, hideMtl);
	if (hideMtl >= 0) {
		int dropped = 0;
		for (int pi = 0; pi < high->packets.GetCount(); pi++) {
			if ((int)high->packets[pi].material_index == hideMtl) {
				high->packets[pi].strips.Reset();
				dropped++;
			}
		}
		Displayf("rscview: -hidemtl %d dropped %d packets", hideMtl, dropped);
	}
	if (ARGS.Get("nocpv")) {
		for (int pi = 0; pi < high->packets.GetCount(); pi++)
			for (int a = 0; a < high->packets[pi].adjuncts.GetCount(); a++)
				high->packets[pi].adjuncts[a].color_idx = 0xFFFFFFFFu;
		Displayf("rscview: -nocpv: vertex colours ignored");
	}
	int cpvAlpha = -1;
	ARGS.Get("cpvalpha", -1, cpvAlpha);
	if (cpvAlpha >= 0) {
		u32 forced = (u32)(cpvAlpha > 255 ? 255 : cpvAlpha);
		for (int ci = 0; ci < high->colors.GetCount(); ci++)
			high->colors[ci] = (high->colors[ci] & 0x00ffffffu) | (forced << 24);
		Displayf("rscview: -cpvalpha: every vertex colour alpha set to %u", forced);
	}
	if (ARGS.Get("notex")) {
		for (int m = 0; m < high->materials.GetCount(); m++) {
			high->materials[m].texture_name.clear();
			high->materials[m].texture_name2.clear();
		}
		Displayf("rscview: -notex: materials untextured");
	}
	if (ARGS.Get("list")) {
		// Per material: real triangles, and how many have every vertex colour alpha
		// under 8 - the ones an alpha test on vertex alpha would throw away.
		for (int m = 0; m < high->materials.GetCount(); m++) {
			int tris = 0, lowAlpha = 0;
			for (int pi = 0; pi < high->packets.GetCount(); pi++) {
				const gfxModelPacket &pk = high->packets[pi];
				if ((int)pk.material_index != m)
					continue;
				const u32 nAdj = (u32)pk.adjuncts.GetCount();
				for (int si = 0; si < pk.strips.GetCount(); si++) {
					const gfxModelStrip &st = pk.strips[si];
					for (int j = 0; j + 2 < st.indices.GetCount(); j++) {
						const u32 ids[3] = { st.indices[j], st.indices[j + 1], st.indices[j + 2] };
						if (ids[0] == ids[1] || ids[1] == ids[2] || ids[0] == ids[2])
							continue;
						if (ids[0] >= nAdj || ids[1] >= nAdj || ids[2] >= nAdj)
							continue;
						tris++;
						u32 amax = 0;
						for (int k = 0; k < 3; k++) {
							u32 c = pk.adjuncts[ids[k]].color_idx;
							u32 al = (c < (u32)high->colors.GetCount()) ? (u32)(high->colors[c] >> 24) : 0x80u;
							if (al > amax)
								amax = al;
						}
						if (amax < 8)
							lowAlpha++;
					}
				}
			}
			Displayf("  material %2d: %d triangles, %d with every vertex alpha below 8", m, tris, lowAlpha);
		}
	}

	// -stripdump: every loaded strip, per material, as index lists - to diff
	// against the .mesh text strip by strip.
	if (ARGS.Get("stripdump")) {
		for (int pi = 0; pi < high->packets.GetCount(); pi++) {
			const gfxModelPacket &pk = high->packets[pi];
			Displayf("[STRIPS] material %d packet %d: %d strips", (int)pk.material_index, pi, pk.strips.GetCount());
			for (int si = 0; si < pk.strips.GetCount(); si++) {
				const gfxModelStrip &st = pk.strips[si];
				char line[2048];
				formatf(line, sizeof(line), "[STRIP] m%d s%d n%d:", (int)pk.material_index, si, st.indices.GetCount());
				size_t len = strlen(line);
				for (int k = 0; k < st.indices.GetCount() && len + 16 < sizeof(line); k++) {
					formatf(line + len, sizeof(line) - len, " %u", st.indices[k]);
					len += strlen(line + len);
				}
				Displayf("%s", line);
			}
		}
	}

	// Framing box.  The parts are bone-local, so place each packet on its bone first.
	s_Min.Set(1e30f, 1e30f, 1e30f);
	s_Max.Set(-1e30f, -1e30f, -1e30f);
	for (int pi = 0; pi < high->packets.GetCount(); pi++) {
		const gfxModelPacket &pk = high->packets[pi];
		int bone = (pk.bone_map.GetCount() > 0) ? (int)pk.bone_map[0] : 0;
		if (bone < 0 || bone >= numBones)
			bone = 0;
		const Matrix34 &bm = sPalette[bone];
		for (int a = 0; a < pk.adjuncts.GetCount(); a++) {
			u32 vi = pk.adjuncts[a].vertex_idx;
			if (vi >= (u32)high->vertices.GetCount())
				continue;
			const Vector3 &v = high->vertices[vi];
			Vector3 p(v.x * bm.a.x + v.y * bm.b.x + v.z * bm.c.x + bm.d.x,
			          v.x * bm.a.y + v.y * bm.b.y + v.z * bm.c.y + bm.d.y,
			          v.x * bm.a.z + v.y * bm.b.z + v.z * bm.c.z + bm.d.z);
			if (p.x < s_Min.x) s_Min.x = p.x;
			if (p.y < s_Min.y) s_Min.y = p.y;
			if (p.z < s_Min.z) s_Min.z = p.z;
			if (p.x > s_Max.x) s_Max.x = p.x;
			if (p.y > s_Max.y) s_Max.y = p.y;
			if (p.z > s_Max.z) s_Max.z = p.z;
		}
	}
	if (s_Min.x > s_Max.x) {
		Errorf("rscview: '%s' has no high LOD vertices", name);
		return 1;
	}

	// Each material draws in its own bucket (gfxModel::Draw filters by it).
	u32 bucketMask = 0;
	for (int m = 0; m < high->materials.GetCount(); m++) {
		int b = high->materials[m].drawbucket;
		if (b < 0) b = 0;
		if (b < 32) bucketMask |= 1u << b;
	}
	if (!bucketMask)
		bucketMask = 1;

	Displayf("rscview: ambient '%s': %d bones, %d wheels, drop shadow bone %d, %d packets, %d materials, buckets %#x, box (%.2f %.2f %.2f)-(%.2f %.2f %.2f)",
	         name, numBones, numWheels, shadowBone, high->packets.GetCount(), high->materials.GetCount(), bucketMask,
	         s_Min.x, s_Min.y, s_Min.z, s_Max.x, s_Max.y, s_Max.z);
	if (ARGS.Get("list")) {
		for (int i = 0; i < shaders.GetCount(); i++)
			Displayf("  shader %2d: %-40s %s", i, shaders[i].templ, shaders[i].tex);
		for (int m = 0; m < high->materials.GetCount(); m++) {
			const gfxModelMaterial &mat = high->materials[m];
			Displayf("  material %2d '%s' texture '%s' bucket %d diffuse (%.2f %.2f %.2f)%s%s", m, mat.name.c_str(), mat.texture_name.c_str(),
			         mat.drawbucket, mat.diffuse[0], mat.diffuse[1], mat.diffuse[2], mat.unlit ? " unlit" : "", mat.no_zwrite ? " no-zwrite" : "");
		}
		for (int i = 0; i < numBones; i++)
			Displayf("  bone %2d %-24s at (%.3f %.3f %.3f)", i, skelData->GetBone(i)->GetName(), sPalette[i].d.x, sPalette[i].d.y, sPalette[i].d.z);
		for (int i = 0; i < bodyColors.GetCount(); i++)
			Displayf("  body colour %d: %.3f %.3f %.3f", i, bodyColors[i].x, bodyColors[i].y, bodyColors[i].z);
	}
	if (ARGS.Get("nogfx"))
		return 0;

	int w = 1280, h = 720;
	ARGS.Get("width", w, w);
	ARGS.Get("height", h, h);
	PIPE.SetRes(w, h, 32);
	PIPE.InitClass();

	Vector3 center = (s_Min + s_Max) * 0.5f;
	float radius = (s_Max - s_Min).Mag() * 0.5f;
	if (radius < 1.0f) radius = 1.0f;
	float dist = radius * 1.55f;
	const char *ds = 0;
	if (ARGS.Get("dist", 0, &ds) && ds) dist = (float)atof(ds);
	float camTheta = 0.6f, camPhi = 0.35f;
	const char *ys = 0, *ps = 0;
	if (ARGS.Get("yaw", 0, &ys) && ys) camTheta = (float)atof(ys) * 3.14159265f / 180.0f;
	if (ARGS.Get("pitch", 0, &ps) && ps) camPhi = (float)atof(ps) * 3.14159265f / 180.0f;
	bool orbit = !ARGS.Get("noorbit") && !ys;

	// -wheelspin [deg/s]: roll the wheel bones the way the game does in motion.
	float spinDegPerSec = 0.0f;
	const char *spinArg = 0;
	if (ARGS.Get("wheelspin", 0, &spinArg))
		spinDegPerSec = (spinArg && spinArg[0] && spinArg[0] != '-' && spinArg[0] != '/') ? (float)atof(spinArg) : 360.0f;

	devPolarCam cam;
	cam.Init(dist, camTheta, camPhi);
	cam.SetOffset(center);
	ageClearColor = 0x203040;
	const char *bg = 0;
	if (ARGS.Get("bg", 0, &bg) && bg) ageClearColor = (u32)strtoul(bg, 0, 16);
	s_Center = center;

	rmcLightGroup lights;
	lights.Reset();
	lights.Ambient.Set(0.45f, 0.45f, 0.48f);
	lights.Mode[0] = 1;                                  // plain directional
	lights.Intensity[0] = 1.0f;
	lights.Color[0].Set(0.85f, 0.83f, 0.80f);

	float az = camTheta;
	float wheelAngle = 0.0f;
	do {
		if (orbit) az += 0.004f;
		cam.SetAzimuth(az);
		cam.Update();
		s_CamPos = cam.GetWorldMtx().d;

		// Key light above and a little behind-left of the camera, as the pack view lights.
		Vector3 toCam = s_CamPos - center;
		toCam.y = 0.0f;
		if (toCam.MagSq() < 1e-6f) toCam.Set(0.0f, 0.0f, 1.0f); else toCam.Normalize();
		Vector3 up(0.0f, 1.0f, 0.0f), right;
		right.Cross(up, toCam);
		Vector3 towardLight = toCam * 0.55f + up * 1.0f + right * 0.45f;
		towardLight.Normalize();
		lights.Dir[0].Set(-towardLight.x, -towardLight.y, -towardLight.z);   // the way the light travels

		for (int i = 0; i < numBones; i++)
			sPalette[i] = skel->GetGlobalMtx(i);
		if (spinDegPerSec != 0.0f) {
			wheelAngle += spinDegPerSec * (3.14159265f / 180.0f) / 60.0f;
			const float sn = sinf(wheelAngle), cs = cosf(wheelAngle);
			Matrix34 roll;
			roll.Identity();
			roll.b.Set(0.0f, cs, -sn);                       // aiAmbientVehicle::Render's wheel roll
			roll.c.Set(0.0f, sn, cs);
			for (int wI = 0; wI < numWheels; wI++) {
				Matrix34 tmp = sPalette[wheelBones[wI]];
				sPalette[wheelBones[wI]].Dot3x3(roll, tmp);
			}
		}

		ageBeginFrame();
		RSTATE.SetCamera(cam.GetWorldMtx());
		if (PIPE.GetViewport()) PIPE.GetViewport()->SetPerspective(60.0f * 3.14159265f / 180.0f, 0.5f, dist * 4.0f + radius * 4.0f);
		RSTATE.SetTexture(0);
		RSTATE.SetLighting(false);
		if (!ARGS.Get("noground")) DrawGround();

		RSTATE.SetZWriteEnable(true);
		RSTATE.SetLighting(true);
		RSTATE.SetLights(lights);
		RSTATE.SetLightingMode(rmclmDirectional);
		for (int b = 0; b < 32; b++)
			if (bucketMask & (1u << b))
				model->Draw(locals, sPalette[0], b, 0, &enables);
		RSTATE.SetLighting(false);
		ageEndFrame();
	} while (!ageExit());

	return 0;
}


//-----------------------------------------------------------------------------
// -ped <type>: one MC3 city pedestrian, loaded and animated the way the game
// does it.
//
// This is the resource viewer's orbit camera around testanim's playback loop.
// The two halves of a ped come from opposite ends of the disc: the ped itself
// is resourced, so its drawable and skeleton only exist inside
// resources/city/<city>_peds.pck and have to come out through
// mcCreatureTypeManager::LoadCityPedResource; its clips are loose, one .anim
// per file under ped/anim, and which clip is which is tune/ped/<city>/
// <name>.cal - a tab separated table mapping an animation type ("walk", "run",
// "idle") to the file that plays it.
//
// Drawing is mcCreature::Render with the parts it cannot have here taken out:
// no LOD pick, no frustum or occluder test, no IK, no per-ped light probe.
// What is left is the part worth looking at - pose the skeleton from the
// animation frame, Update() to resolve the bone globals, DrawSkinned through
// them.
//-----------------------------------------------------------------------------

// Case insensitive substring, for matching a ped by part of its name.
static const char *sStrIStr(const char *hay, const char *needle)
{
	if (!hay || !needle || !needle[0])
		return 0;
	size_t n = strlen(needle);
	for (; *hay; hay++)
		if (!_strnicmp(hay, needle, n))
			return hay;
	return 0;
}

struct rscPedClip {
	char type[32];          // "walk", "idle", ...
	char file[64];          // the clip under ped/anim
	bool transformed;       // the clip carries the ped forward
	bool loop;
};

// tune/ped/<city>/<base>.cal.  Five tab separated columns under a fixed header
// row; no field holds a space, so whitespace tokens are enough to read it.
static int sReadPedClips(const char *city, const char *base, atArray<rscPedClip> &out)
{
	out.Reset();
	char path[160];
	formatf(path, sizeof(path), "$/tune/ped/%s/%s", city, base);
	Stream *s = ASSET.Open(path, "cal");
	if (!s)
		return 0;
	datTokenizer tok;
	tok.Init(path, s);
	char t[128];
	// Skip the header row: it ends with "Lateral".
	for (;;) {
		t[0] = 0;
		if (!tok.GetToken(t, sizeof(t)) || !t[0]) { s->Close(); return 0; }
		if (!_stricmp(t, "Lateral"))
			break;
	}
	for (;;) {
		rscPedClip clip;
		clip.type[0] = 0;
		clip.file[0] = 0;
		if (!tok.GetToken(clip.type, sizeof(clip.type)) || !clip.type[0])
			break;
		if (!tok.GetToken(clip.file, sizeof(clip.file)) || !clip.file[0])
			break;
		t[0] = 0; tok.GetToken(t, sizeof(t));  clip.transformed = !_stricmp(t, "yes");
		t[0] = 0; tok.GetToken(t, sizeof(t));  clip.loop        = !_stricmp(t, "yes");
		t[0] = 0; tok.GetToken(t, sizeof(t));  // lateral, unused here
		out.Append(clip);
	}
	s->Close();
	return out.GetCount();
}

// A creature type is named with the city in front of it - SANmped01, ATLmped01 -
// while its clip table is filed under the bare model name.  Try the name as it
// stands, then from the gender letter on, then the stock male ped.
static bool sFindPedCal(const char *city, const char *typeName, char *out, size_t outSize)
{
	const char *candidates[3];
	int numCandidates = 0;
	if (typeName && typeName[0])
		candidates[numCandidates++] = typeName;
	const char *body = typeName ? strstr(typeName, "ped") : 0;
	if (body && body > typeName + 1)
		candidates[numCandidates++] = body - 1;         // the gender letter and on
	candidates[numCandidates++] = "mped01";

	char probe[160];
	for (int i = 0; i < numCandidates; i++) {
		if (!candidates[i] || !candidates[i][0])
			continue;
		formatf(probe, sizeof(probe), "$/tune/ped/%s/%s", city, candidates[i]);
		if (ASSET.Exists(probe, "cal")) {
			formatf(out, (int)outSize, "%s", candidates[i]);
			return true;
		}
	}
	return false;
}

static int RunPedViewer(const char *nameArg)
{
	const char *city = "sd";
	ARGS.Get("city", 0, &city);
	if (!city || !city[0] || city[0] == '-' || city[0] == '/')
		city = "sd";

	mcCreatureTypeManager mgr;
	if (!mgr.LoadCityPedResource(city)) {
		Errorf("rscview: no peds for city '%s' (expected resources/city/%s_peds.pck); the cities are sd, atlanta, detroit and tokyo",
		       city, city);
		return 1;
	}
	const int numTypes = mgr.GetNumCreatureTypes();

	// -ped with a name picks by name (substring, case insensitive), with a
	// number picks by index, bare takes the first type the pack holds.
	int typeIndex = 0;
	const bool bare = !nameArg || !nameArg[0] || nameArg[0] == '-' || nameArg[0] == '/';
	if (!bare) {
		char *end = 0;
		long asIndex = strtol(nameArg, &end, 10);
		if (end && !*end) {
			typeIndex = (int)asIndex;
		} else {
			typeIndex = -1;
			for (int i = 0; i < numTypes; i++) {
				const mcCreatureType *t = mgr.GetCreatureType(i);
				const char *n = t ? t->GetName() : 0;
				if (n && sStrIStr(n, nameArg)) { typeIndex = i; break; }
			}
			if (typeIndex < 0) {
				Errorf("rscview: '%s' is not a ped of city '%s'; the types are:", nameArg, city);
				for (int i = 0; i < numTypes; i++) {
					const mcCreatureType *t = mgr.GetCreatureType(i);
					Displayf("    %d: %s", i, (t && t->GetName()) ? t->GetName() : "(unnamed)");
				}
				return 1;
			}
		}
	}
	if (typeIndex < 0 || typeIndex >= numTypes) {
		Errorf("rscview: ped %d is outside the %d types of city '%s'", typeIndex, numTypes, city);
		return 1;
	}

	const mcCreatureType *type = mgr.GetCreatureType(typeIndex);
	if (!type) {
		Errorf("rscview: ped %d of city '%s' did not build", typeIndex, city);
		return 1;
	}
	const char *typeName = type->GetName() ? type->GetName() : "";

	rmcDrawable &drawable = type->GetRmDrawable();
	crSkeleton &skel = type->GetSkeleton();
	if (!skel.HasSkeletonData()) {
		Errorf("rscview: ped '%s' came out of the pack without a skeleton", typeName);
		return 1;
	}
	crSkeletonData &skelData = skel.GetSkeletonData();
	const int numBones = skelData.GetNumBones();

	// Which clip.  A name that is a file under ped/anim plays as it stands;
	// anything else is an animation type looked up in this ped's .cal.
	const char *animArg = "walk";
	ARGS.Get("anim", 0, &animArg);
	if (!animArg || !animArg[0] || animArg[0] == '-' || animArg[0] == '/')
		animArg = "walk";

	char calBase[64] = "";
	atArray<rscPedClip> clips;
	if (sFindPedCal(city, typeName, calBase, sizeof(calBase)))
		sReadPedClips(city, calBase, clips);

	char animFile[64];
	formatf(animFile, sizeof(animFile), "%s", animArg);
	bool looping = true, transformed = true;
	char probe[160];
	formatf(probe, sizeof(probe), "$/ped/anim/%s", animArg);
	if (!ASSET.Exists(probe, "anim")) {
		const rscPedClip *found = 0;
		for (int i = 0; i < clips.GetCount(); i++)
			if (!_stricmp(clips[i].type, animArg)) { found = &clips[i]; break; }
		if (!found) {
			Errorf("rscview: '%s' is neither a clip under ped/anim nor an animation type of '%s.cal'", animArg, calBase);
			for (int i = 0; i < clips.GetCount(); i++)
				Displayf("    %-24s %s", clips[i].type, clips[i].file);
			return 1;
		}
		formatf(animFile, sizeof(animFile), "%s", found->file);
		looping = found->loop;
		transformed = found->transformed;
	}

	rscAnimation *anim = rscAnimation::GetAnimationPrefix("ped/anim", animFile, &skelData);
	if (!anim) {
		Errorf("rscview: could not load ped/anim/%s.anim", animFile);
		return 1;
	}
	const int numFrames = anim->GetNumFrames();
	if (numFrames <= 0) {
		Errorf("rscview: ped/anim/%s.anim holds no frames", animFile);
		anim->Release();
		return 1;
	}

	Displayf("rscview: ped '%s' (%d of %d, city %s): %d bones, clip '%s' %d frames %d channels, stride %.2fm%s",
	         typeName, typeIndex, numTypes, city, numBones, animFile, numFrames, anim->GetNumChannels(),
	         anim->GetStrideLength(), looping ? "" : ", one-shot");
	if (ARGS.Get("list")) {
		for (int i = 0; i < numTypes; i++) {
			const mcCreatureType *t = mgr.GetCreatureType(i);
			Displayf("  ped %d: %s", i, (t && t->GetName()) ? t->GetName() : "(unnamed)");
		}
		for (int i = 0; i < clips.GetCount(); i++)
			Displayf("  clip %-24s %-28s%s%s", clips[i].type, clips[i].file,
			         clips[i].transformed ? " transformed" : "", clips[i].loop ? " loop" : "");
		for (int i = 0; i < numBones; i++) {
			const crBoneData *bd = skelData.GetBone(i);
			Displayf("  bone %2d %s", i, (bd && bd->GetName()) ? bd->GetName() : "(unnamed)");
		}
		for (int l = 0; l < 4; l++) {
			const gfxModel *m = drawable.GetModel(l);
			Displayf("  lod %d model %p%s", l, (const void *)m, m ? "" : "  (nothing at this lod)");
		}
	}
	if (ARGS.Get("nogfx")) {
		anim->Release();
		return 0;
	}

	// Frame the camera on the mesh, not the skeleton: the bones stop at the
	// joints, so a box drawn around them cuts the top of the head off and sits
	// the ped too high.
	static Matrix34 sPedRoot;
	sPedRoot.Identity();
	skel.SetParentMtx(&sPedRoot);
	anim->GetFrame(0).Pose(skel);
	skel.Update();
	Vector3 lo(1e9f, 1e9f, 1e9f), hi(-1e9f, -1e9f, -1e9f);
	const gfxModel *sizeModel = drawable.GetModel(0);
	if (sizeModel)
		sizeModel->GetBoundingBox(lo, hi);
	if (hi.y - lo.y < 0.1f) {
		for (int i = 0; i < numBones; i++) {
			const Vector3 &p = skel.GetGlobalMtx(i).d;
			if (p.x < lo.x) lo.x = p.x;
			if (p.x > hi.x) hi.x = p.x;
			if (p.y < lo.y) lo.y = p.y;
			if (p.y > hi.y) hi.y = p.y;
			if (p.z < lo.z) lo.z = p.z;
			if (p.z > hi.z) hi.z = p.z;
		}
	}
	if (hi.y - lo.y < 0.1f) {                    // no usable spread: a ped sized box
		lo.Set(-0.4f, 0.0f, -0.4f);
		hi.Set(0.4f, 1.8f, 0.4f);
	}
	s_Min = lo;
	s_Max = hi;

	int w = 1280, h = 720;
	ARGS.Get("width", w, w);
	ARGS.Get("height", h, h);
	PIPE.SetRes(w, h, 32);
	PIPE.InitClass();

	Vector3 center = (s_Min + s_Max) * 0.5f;
	float radius = (s_Max - s_Min).Mag() * 0.5f;
	if (radius < 0.5f) radius = 0.5f;
	// A ped is tall and thin, so the height is what has to fit, not the
	// diagonal: framing off the radius pushes the camera in far enough to cut
	// the head and feet off.
	float dist = (s_Max.y - s_Min.y) * 1.25f;
	if (dist < radius * 1.5f) dist = radius * 1.5f;
	const char *ds = 0;
	if (ARGS.Get("dist", 0, &ds) && ds) dist = (float)atof(ds);
	float camTheta = 0.6f, camPhi = 0.2f;
	const char *ys = 0, *ps = 0;
	if (ARGS.Get("yaw", 0, &ys) && ys) camTheta = (float)atof(ys) * 3.14159265f / 180.0f;
	if (ARGS.Get("pitch", 0, &ps) && ps) camPhi = (float)atof(ps) * 3.14159265f / 180.0f;
	const bool orbit = !ARGS.Get("noorbit") && !ys;

	// Playback.  Clips are authored at 30fps and the viewer runs at 60, so a
	// rate of 1 is the speed the game plays them at; -pause holds, -frame N
	// pins one frame.  A transformed clip walks the ped away from the camera,
	// so its root translation is dropped by default and the ped walks on the
	// spot; -stride lets it travel and carries the camera along with it.
	float animRate = 1.0f;
	const char *rateArg = 0;
	if (ARGS.Get("animrate", 0, &rateArg) && rateArg) animRate = (float)atof(rateArg);
	const bool paused = ARGS.Get("pause");
	int fixedFrame = -1;
	ARGS.Get("frame", -1, fixedFrame);
	const bool travel = ARGS.Get("stride") && transformed;
	int lod = 0;
	ARGS.Get("lod", 0, lod);
	int variant = 0;
	ARGS.Get("variant", 0, variant);
	int bucket = 0;
	ARGS.Get("bucket", 0, bucket);

	devPolarCam cam;
	cam.Init(dist, camTheta, camPhi);
	cam.SetOffset(center);
	ageClearColor = 0x203040;
	const char *bg = 0;
	if (ARGS.Get("bg", 0, &bg) && bg) ageClearColor = (u32)strtoul(bg, 0, 16);
	s_Center = center;

	rmcLightGroup lights;
	lights.Reset();
	lights.Ambient.Set(0.45f, 0.45f, 0.48f);
	lights.Mode[0] = 1;                                  // plain directional
	lights.Intensity[0] = 1.0f;
	lights.Color[0].Set(0.85f, 0.83f, 0.80f);

	rscAnimFrame frame;
	frame.Init(anim->GetNumChannels());

	float az = camTheta;
	float phase = 0.0f;                                  // 0..1 through the clip
	const float clipSeconds = (float)numFrames / 30.0f;
	Vector3 walked(0.0f, 0.0f, 0.0f);
	do {
		if (orbit) az += 0.004f;
		cam.SetAzimuth(az);
		cam.SetOffset(center + walked);
		cam.Update();
		s_CamPos = cam.GetWorldMtx().d;

		// Key light above and a little behind-left of the camera, as the pack
		// and ambient views light.
		Vector3 toCam = s_CamPos - (center + walked);
		toCam.y = 0.0f;
		if (toCam.MagSq() < 1e-6f) toCam.Set(0.0f, 0.0f, 1.0f); else toCam.Normalize();
		Vector3 up(0.0f, 1.0f, 0.0f), right;
		right.Cross(up, toCam);
		Vector3 towardLight = toCam * 0.55f + up * 1.0f + right * 0.45f;
		towardLight.Normalize();
		lights.Dir[0].Set(-towardLight.x, -towardLight.y, -towardLight.z);

		if (fixedFrame >= 0) {
			phase = (numFrames > 1) ? (float)(fixedFrame < numFrames - 1 ? fixedFrame : numFrames - 1) / (float)(numFrames - 1) : 0.0f;
		} else if (!paused) {
			phase += (animRate / 60.0f) / (clipSeconds > 0.0f ? clipSeconds : 1.0f);
			if (phase >= 1.0f) {
				if (looping) {
					phase -= 1.0f;
					if (travel) walked.Add(anim->GetStride());
				} else {
					phase = 1.0f;
				}
			}
		}

		// mcCreature::Render's pose: a frame onto the skeleton, then forward
		// kinematics through it.
		anim->GetBlendFrame(frame, phase, &skelData, looping ? 1 : 0);
		if (!travel && frame.GetNumChannels() >= 3) {
			// Channels 0..2 are the root translation.  Dropping the two
			// horizontal ones keeps a walk cycle on the spot rather than
			// marching it out of frame; the vertical one is the ped bobbing.
			frame.SetTransData(0, 0.0f);
			frame.SetTransData(2, 0.0f);
		}
		sPedRoot.Identity();
		if (travel)
			sPedRoot.d.Set(walked.x, walked.y, walked.z);
		skel.SetParentMtx(&sPedRoot);
		frame.Pose(skel);
		skel.Update();

		ageBeginFrame();
		RSTATE.SetCamera(cam.GetWorldMtx());
		if (PIPE.GetViewport()) PIPE.GetViewport()->SetPerspective(60.0f * 3.14159265f / 180.0f, 0.1f, dist * 4.0f + radius * 8.0f);
		RSTATE.SetTexture(0);
		RSTATE.SetLighting(false);
		if (!ARGS.Get("noground")) DrawGround();

		RSTATE.SetZWriteEnable(true);
		RSTATE.SetLighting(true);
		RSTATE.SetLights(lights);
		RSTATE.SetLightingMode(rmclmDirectional);
		drawable.DrawSkinned(NULL, skel, bucket, lod, variant);
		RSTATE.SetLighting(false);
		ageEndFrame();
	} while (!ageExit());

	anim->Release();
	return 0;
}
//-----------------------------------------------------------------------------
// City geometry browser mode:
// Loads a city pack, resolves model names from the city's hood/type definitions,
// and provides interactive arrow-key navigation with on-screen HUD text.
//-----------------------------------------------------------------------------

static bool ResolveCityPack(const char *name, char *outBuf, size_t bufSize)
{
	if (!name || !name[0] || name[0] == '-' || name[0] == '/') {
		formatf(outBuf, bufSize, "resources/city/sd_midnight_clear");
		return true;
	}
	if (strstr(name, "resources/city/") || strstr(name, "/") || strstr(name, "\\") || strstr(name, ".pck")) {
		strncpy(outBuf, name, bufSize - 1);
		outBuf[bufSize - 1] = 0;
		char *dot = strstr(outBuf, ".pck");
		if (dot) *dot = 0;
		return true;
	}
	const char *weather = "clear";
	const char *timeOfDay = "midnight";
	ARGS.Get("weather", 0, &weather);
	ARGS.Get("time", 0, &timeOfDay);

	const char *prefix = name;
	if (!_stricmp(name, "sandiego") || !_stricmp(name, "sd"))
		prefix = "sd";
	else if (!_stricmp(name, "detroit"))
		prefix = "detroit";
	else if (!_stricmp(name, "atlanta"))
		prefix = "atlanta";
	else if (!_stricmp(name, "tokyo"))
		prefix = "tokyo";

	formatf(outBuf, bufSize, "resources/city/%s_%s_%s", prefix, timeOfDay, weather);
	return true;
}

static void rscScanCityModelNames(const datResourceImage &image, std::unordered_map<u32, std::string> &nameMap)
{
	nameMap.clear();
	u32 base = image.GetBase();
	if (!base || !image.IsValidAddress(base, 0x80)) return;

	int numHoods = (int)image.ReadU32(base + 0x14);
	u32 pHoods = image.ReadU32(base + 0x18);
	if (numHoods <= 0 || numHoods > 64 || !pHoods || !image.IsValidAddress(pHoods, (u32)numHoods * 20)) return;

	static const char *kPartNames[5] = { "MAIN", "GROUND", "REFLECT", "HDR", "ALPHA" };
	std::unordered_set<u32> seenTypes;

	for (int h = 0; h < numHoods; h++) {
		u32 hAddr = pHoods + (u32)h * 20;
		if (!image.IsValidAddress(hAddr, 20)) continue;
		int numCityModels = (int)image.ReadU32(hAddr + 4);
		u32 pCityModels = image.ReadU32(hAddr + 8);
		int numInstCityModels = (int)image.ReadU32(hAddr + 12);
		u32 pInstCityModels = image.ReadU32(hAddr + 16);

		// Unique city models (stride 28 = 0x1c)
		if (numCityModels > 0 && pCityModels && image.IsValidAddress(pCityModels, (u32)numCityModels * 28)) {
			for (int m = 0; m < numCityModels; m++) {
				u32 mAddr = pCityModels + (u32)m * 28;
				u32 typeAddr = image.ReadU32(mAddr + 0x0c);
				if (!typeAddr || seenTypes.find(typeAddr) != seenTypes.end()) continue;
				seenTypes.insert(typeAddr);
				if (!image.IsValidAddress(typeAddr, 0x64)) continue;

				u32 namePtr = image.ReadU32(typeAddr + 0x08);
				const char *typeName = (namePtr && image.IsValidAddress(namePtr, 1)) ? image.ReadString(namePtr) : "";
				if (!typeName || !typeName[0]) continue;

				for (int lod = 0; lod < 2; lod++) {
					for (int part = 0; part < 5; part++) {
						u32 modelAddr = image.ReadU32(typeAddr + 0x3c + (lod * 5 + part) * 4);
						if (modelAddr && nameMap.find(modelAddr) == nameMap.end()) {
							char buf[128];
							formatf(buf, sizeof(buf), "%s [LOD %d %s]", typeName, lod, kPartNames[part]);
							nameMap[modelAddr] = buf;
							s_CityModelPart[modelAddr] = part;
						}
					}
				}
			}
		}

		// Instanced city models (stride 96 = 0x60)
		if (numInstCityModels > 0 && pInstCityModels && image.IsValidAddress(pInstCityModels, (u32)numInstCityModels * 96)) {
			for (int m = 0; m < numInstCityModels; m++) {
				u32 mAddr = pInstCityModels + (u32)m * 96;
				u32 typeAddr = image.ReadU32(mAddr + 0x0c);
				if (!typeAddr || seenTypes.find(typeAddr) != seenTypes.end()) continue;
				seenTypes.insert(typeAddr);
				if (!image.IsValidAddress(typeAddr, 0x64)) continue;

				u32 namePtr = image.ReadU32(typeAddr + 0x08);
				const char *typeName = (namePtr && image.IsValidAddress(namePtr, 1)) ? image.ReadString(namePtr) : "";
				if (!typeName || !typeName[0]) continue;

				for (int lod = 0; lod < 2; lod++) {
					for (int part = 0; part < 5; part++) {
						u32 modelAddr = image.ReadU32(typeAddr + 0x3c + (lod * 5 + part) * 4);
						if (modelAddr && nameMap.find(modelAddr) == nameMap.end()) {
							char buf[128];
							formatf(buf, sizeof(buf), "%s [LOD %d %s]", typeName, lod, kPartNames[part]);
							nameMap[modelAddr] = buf;
							s_CityModelPart[modelAddr] = part;
						}
					}
				}
			}
		}
	}
}

// -loadcity: build the city the way the game does, then draw all of it at once.
//
// The game drove this per frame - the PVS picking visible cells for a moving camera, several
// passes over them - and none of that machinery is in this tree.  What survives is enough:
// every model carries its own world placement, and the render path has a no-PVS branch that
// simply walks the instances a type holds.  So activate every model once, turn the PVS off,
// and the city draws as a single object.
static int RunCityRender(const char *pack)
{
	mcCity *city = NULL;
	datChunk::Load(city, pack, CITY_RESOURCE_VERSION);
	if (!city) {
		Errorf("rscview: failed to load city resource '%s' (no datResourceBuilder<mcCity>)", pack);
		return 1;
	}
	Displayf("rscview: successfully built mcCity from '%s'!", pack);

	// the city draw path pushes this factory before every pass
	if (!MCGFX) {
		MCGFX = new mcGfx();
		MCGFX->Init();
	}

	// The builder already enables every model it places, which is what puts it on its type's
	// active list.  Enable is idempotent - it asks IsEnabled first - so this only catches any
	// the builder left out; calling AddToActive directly would add a second time and leave the
	// list pointing into itself.
	int nCity = 0, nInst = 0;
	Vector3 vMin(1e30f, 1e30f, 1e30f), vMax(-1e30f, -1e30f, -1e30f);
	for (int hd = 0; hd < city->GetNumHoods(); hd++) {
		const mcHood &hood = city->GetHood(hd);
		for (int m = 0; m < hood.GetNumCityModels(); m++) {
			mcCityModel *cm = hood.GetCityModel(m);
			if (!cm || !cm->GetType()) continue;
			cm->Enable(true);
			nCity++;
			const Vector3 &c = cm->GetCenter();
			float r = cm->GetRadius();
			if (c.x - r < vMin.x) vMin.x = c.x - r;
			if (c.y - r < vMin.y) vMin.y = c.y - r;
			if (c.z - r < vMin.z) vMin.z = c.z - r;
			if (c.x + r > vMax.x) vMax.x = c.x + r;
			if (c.y + r > vMax.y) vMax.y = c.y + r;
			if (c.z + r > vMax.z) vMax.z = c.z + r;
		}
		for (int m = 0; !ARGS.Get("cityonly") && m < hood.GetNumInstCityModels(); m++) {
			mcInstCityModel *im = hood.GetInstCityModel(m);
			if (!im || !im->GetType()) continue;
			im->Enable(true);
			nInst++;
		}
	}
	city->SetUsePVS(false);

	// The game's distance cull is sized for a car on the ground; a whole-city view wants the lot.
	Vector3 span = vMax - vMin;
	float drawDist = span.Mag();
	ARGS.Get("citydist", drawDist, drawDist);
	city->SetNoPVSDrawDist(drawDist);

	// A city runs under conditions loaded from a file the viewer does not read, and the
	// defaults in mcConditionVariables are white fog from 200 to 300 units.  Over a city three
	// kilometres wide that paints the whole thing solid white, which is not a texturing
	// problem however much it looks like one.  Fog goes to the background colour and out past
	// the draw distance, and the lights come up from the 0.1 grey the defaults carry.
	ageClearColor = 0x203040;
	const char *bgArg = 0;
	if (ARGS.Get("bg", 0, &bgArg) && bgArg) ageClearColor = (u32)strtoul(bgArg, 0, 16);
	{
		mcConditionVariables &cond = city->GetCurrentCondition();
		float fr = ((ageClearColor >> 16) & 255) / 255.0f;
		float fg = ((ageClearColor >> 8) & 255) / 255.0f;
		float fb = (ageClearColor & 255) / 255.0f;
		cond.mFogColor.Set(fr, fg, fb);
		cond.mIndoorFogColor.Set(fr, fg, fb);
		cond.mFogStart = drawDist * 0.75f;
		cond.mFogEnd = drawDist * 1.6f;
		cond.mIndoorFogStart = cond.mFogStart;
		cond.mIndoorFogEnd = cond.mFogEnd;
		cond.mAmbientLight.Set(0.45f, 0.45f, 0.50f, 0.0f);
		cond.mDirectionalLightColor.Set(0.80f, 0.78f, 0.70f, 0.0f);
	}

	Vector3 center = (vMin + vMax) * 0.5f;
	Displayf("rscview: city as one object - %d city models, %d instanced, extents "
	         "(%.0f %.0f %.0f)-(%.0f %.0f %.0f), draw distance %.0f",
	         nCity, nInst, vMin.x, vMin.y, vMin.z, vMax.x, vMax.y, vMax.z, drawDist);

	if (ARGS.Get("citytexlog")) {
		int resident = 0, probed = 0;
		for (int i = 0; i < 1600; i++) {
			char nm[64];
			formatf(nm, sizeof(nm), "city_tex_%d", i);
			if (gfxTextureResident(nm)) resident++;
			probed++;
		}
		Displayf("citytexlog: %d of %d city_tex_N names resident", resident, probed);
		int shown = 0;
		for (int hd = 0; hd < city->GetNumHoods() && shown < 4; hd++) {
			const mcHood &hood = city->GetHood(hd);
			for (int m = 0; m < hood.GetNumCityModels() && shown < 4; m++) {
				mcCityModel *cm = hood.GetCityModel(m);
				if (!cm || !cm->GetCityModelType()) continue;
				const rmcModel *mm = cm->GetCityModelType()->GetModel(cm->GetLOD(), partMAIN);
				gfxModel *gm = mm ? const_cast<rmcModel *>(mm)->GetModel(0) : NULL;
				if (!gm) continue;
				Displayf("citytexlog: model '%s' lod %d group %d: %d packets, %d materials",
				         cm->GetCityModelType()->GetName(), cm->GetLOD(), cm->GetGroup(),
				         gm->packets.GetCount(), gm->materials.GetCount());
				for (int mi = 0; mi < gm->materials.GetCount() && mi < 6; mi++) {
					const gfxModelMaterial &mat = gm->materials[mi];
					Displayf("    mat %d '%s' texture '%s' -> %s", mi, mat.name.c_str(),
					         mat.texture_name.c_str(),
					         mat.texture_name.empty() ? "(none)" :
					         (gfxTextureResident(mat.texture_name.c_str()) ? "resident" : "MISSING"));
				}
				shown++;
			}
		}
	}

	if (ARGS.Get("nogfx")) return 0;

	int w = 1280, h = 720;
	ARGS.Get("width", w, w);
	ARGS.Get("height", h, h);
	PIPE.SetRes(w, h, 32);
	PIPE.InitClass();

	devPolarCam cam;
	cam.Init(span.Mag() * 0.6f, 0.7f, 0.5f);
	cam.SetFollow(center);

	do {
		cam.Update();
		ageBeginFrame();
		RSTATE.SetCamera(cam.GetWorldMtx());
		if (PIPE.GetViewport())
			PIPE.GetViewport()->SetPerspective(60.0f * 3.14159265f / 180.0f, 1.0f,
			                                   cam.GetDist() * 4.0f + drawDist * 2.0f);

		// through the base: the render entry points are its public virtuals, and
		// mcInstCityModelClass keeps its own overrides protected
		mcCullableClass *cityClass = city->GetCityModelClass();
		mcCullableClass *instClass = city->GetInstCityModelClass();
		if (cityClass) {
			cityClass->SetRenderStates(dtMAIN_SHADOWED);
			cityClass->RenderAllTypes(dtMAIN_SHADOWED);
			cityClass->RestoreRenderStates(dtMAIN_SHADOWED);
		}
		if (instClass && !ARGS.Get("cityonly")) {
			instClass->SetRenderStates(dtMAIN_SHADOWED);
			instClass->RenderAllTypes(dtMAIN_SHADOWED);
			instClass->RestoreRenderStates(dtMAIN_SHADOWED);
		}
		ageEndFrame();
	} while (!ageExit());

	return 0;
}

static int RunCityBrowser(const char *pack, datResourceImage &image)
{
	atArray<u32> addrs;
	int vt = rscModelVTable;
	ARGS.Get("vtable", vt, vt);
	rscFindModels(image, addrs, (u32)vt);
	if (!addrs.GetCount()) {
		rscFindCarModels(image, addrs);
	}
	if (!addrs.GetCount()) {
		Errorf("rscview: no models found in '%s'", pack);
		return 1;
	}

	std::unordered_map<u32, std::string> nameMap;
	rscScanCityModelNames(image, nameMap);
	{
		atArray<u32> sky;
		rscFindSkyhatModels(image, sky);
		for (int i = 0; i < sky.GetCount(); i++) {
			char buf[64];
			formatf(buf, sizeof(buf), "SKYHAT LAYER %d (sky shader group)", i);
			nameMap[sky[i]] = buf;
		}
	}
	Displayf("rscview: browser mode loaded %d models (%d with names from city headers) in '%s'",
	         addrs.GetCount(), (int)nameMap.size(), pack);

	if (ARGS.Get("nogfx")) return 0;

	int w = 1280, h = 720;
	ARGS.Get("width", w, w);
	ARGS.Get("height", h, h);
	PIPE.SetRes(w, h, 32);
	PIPE.InitClass();

	struct rscBrowseItem {
		u32 addr;
		std::string name;
		rscModel model;
		bool decoded;
	};

	const int count = addrs.GetCount();
	std::vector<rscBrowseItem> items(count);
	for (int i = 0; i < count; i++) {
		items[i].addr = addrs[i];
		items[i].decoded = false;
		auto it = nameMap.find(addrs[i]);
		if (it != nameMap.end()) {
			items[i].name = it->second;
		} else {
			char buf[64];
			formatf(buf, sizeof(buf), "model_%04d @ 0x%08x", i, addrs[i]);
			items[i].name = buf;
		}
	}

	int currentIndex = 0;
	int startModel = -1;
	if (ARGS.Get("model", -1, startModel) && startModel >= 0) {
		currentIndex = startModel;
	}
	if (currentIndex < 0) currentIndex = 0;
	if (currentIndex >= count) currentIndex = count - 1;

	float camTheta = 0.6f;
	float camPhi = 0.35f;
	const char *ys = 0, *ps = 0;
	if (ARGS.Get("yaw", 0, &ys) && ys) camTheta = (float)atof(ys) * 3.14159265f / 180.0f;
	if (ARGS.Get("pitch", 0, &ps) && ps) camPhi = (float)atof(ps) * 3.14159265f / 180.0f;
	bool orbit = !ARGS.Get("noorbit") && !ys;

	devPolarCam cam;
	cam.Init(10.0f, camTheta, camPhi);
	ageClearColor = 0x203040;
	const char *bg = 0;
	if (ARGS.Get("bg", 0, &bg) && bg) ageClearColor = (u32)strtoul(bg, 0, 16);

	RSTATE.SetLighting(false);
	s_Lit = !ARGS.Get("unlit");
	s_Wire = ARGS.Get("wire");

	int activeIndex = -1;
	auto activateModel = [&](int idx) {
		if (idx < 0) idx = 0;
		if (idx >= count) idx = count - 1;
		currentIndex = idx;
		activeIndex = idx;
		rscBrowseItem &cur = items[idx];
		if (!cur.decoded) {
			rscDecodeModel(image, cur.addr, cur.model);
			cur.decoded = true;
		}
		s_Models.Reset();
		if (cur.model.numVerts > 0) {
			s_Models.Append(cur.model);
			s_Min = cur.model.boxMin;
			s_Max = cur.model.boxMax;
		} else {
			s_Min = Vector3(-1.0f, -1.0f, -1.0f);
			s_Max = Vector3(1.0f, 1.0f, 1.0f);
		}
		Vector3 center = (s_Min + s_Max) * 0.5f;
		Vector3 extents = s_Max - s_Min;
		float radius = extents.Mag() * 0.5f;
		if (radius < 0.5f) radius = 0.5f;
		float dist = radius * 2.2f;
		if (dist < 2.0f) dist = 2.0f;
		const char *ds = 0;
		if (ARGS.Get("dist", 0, &ds) && ds) dist = (float)atof(ds);

		cam.SetOffset(center);
		cam.SetDist(dist);
		s_Center = center;
	};

	activateModel(currentIndex);

	float az = camTheta;
	do {
		int newIndex = currentIndex;
		if (ioKeyboard::KeyPressed(KEY_RIGHT)) newIndex++;
		if (ioKeyboard::KeyPressed(KEY_LEFT)) newIndex--;
		if (ioKeyboard::KeyPressed(KEY_UP)) newIndex += 10;
		if (ioKeyboard::KeyPressed(KEY_DOWN)) newIndex -= 10;
		if (ioKeyboard::KeyPressed(KEY_PAGEDOWN)) newIndex += 50;
		if (ioKeyboard::KeyPressed(KEY_PAGEUP)) newIndex -= 50;
		if (ioKeyboard::KeyPressed(KEY_HOME)) newIndex = 0;
		if (ioKeyboard::KeyPressed(KEY_END)) newIndex = count - 1;

		if (newIndex < 0) newIndex = (newIndex % count + count) % count;
		if (newIndex >= count) newIndex = newIndex % count;

		if (ioKeyboard::KeyPressed(KEY_SPACE)) orbit = !orbit;
		if (ioKeyboard::KeyPressed(KEY_W)) s_Wire = !s_Wire;
		if (ioKeyboard::KeyPressed(KEY_L)) s_Lit = !s_Lit;

		if (newIndex != activeIndex) {
			activateModel(newIndex);
		}

		if (orbit) az += 0.005f;
		cam.SetAzimuth(az);
		cam.Update();

		s_CamPos = cam.GetWorldMtx().d;
		{
			Vector3 toCam = s_CamPos - s_Center; toCam.y = 0.0f;
			if (toCam.MagSq() < 1e-6f) toCam.Set(0.0f, 0.0f, 1.0f); else toCam.Normalize();
			Vector3 up(0.0f, 1.0f, 0.0f), right; right.Cross(up, toCam);
			s_LightDir = toCam * 0.55f + up * 1.0f + right * 0.45f;
			s_LightDir.Normalize();
		}

		ageBeginFrame();
		RSTATE.SetCamera(cam.GetWorldMtx());
		float curDist = cam.GetDist();
		float curRad = (s_Max - s_Min).Mag() * 0.5f;
		if (PIPE.GetViewport())
			PIPE.GetViewport()->SetPerspective(60.0f * 3.14159265f / 180.0f, 0.5f, curDist * 4.0f + curRad * 4.0f + 50.0f);
		RSTATE.SetTexture(0);
		DrawModels();

		const rscBrowseItem &cur = items[currentIndex];
		int totalBatches = 0;
		for (int g = 0; g < cur.model.geometries.GetCount(); g++)
			totalBatches += cur.model.geometries[g].batches.GetCount();
		Vector3 size = s_Max - s_Min;

		char l1[256], l2[256], l3[256], l4[256], l5[256];
		formatf(l1, sizeof(l1), "CITY MODEL BROWSER: %s [%d / %d]", pack, currentIndex + 1, count);
		formatf(l2, sizeof(l2), "NAME: %s", cur.name.c_str());
		formatf(l3, sizeof(l3), "ADDR: 0x%08x | GEOMS: %d | BATCHES: %d | VERTS: %d",
		        cur.addr, cur.model.geometries.GetCount(), totalBatches, cur.model.numVerts);
		formatf(l4, sizeof(l4), "BOUNDS: (%.1f, %.1f, %.1f) to (%.1f, %.1f, %.1f) [SIZE: %.1fx%.1fx%.1fm]",
		        s_Min.x, s_Min.y, s_Min.z, s_Max.x, s_Max.y, s_Max.z, size.x, size.y, size.z);
		formatf(l5, sizeof(l5), "ORBIT: %s [Space] | WIREFRAME: %s [W] | SHADING: %s [L] | CAM DIST: %.1fm",
		        orbit ? "ON" : "OFF", s_Wire ? "ON" : "OFF", s_Lit ? "LIT" : "UNLIT", curDist);

		const float hudScale = 1.8f;   // a touch larger than the default 1.4 for legibility
		gfxDrawFontScaled(20, 20,  l1, 0xFF00E5FF, hudScale); // Cyan
		gfxDrawFontScaled(20, 42,  l2, 0xFFFFFFFF, hudScale); // White
		gfxDrawFontScaled(20, 64,  l3, 0xFFFFD600, hudScale); // Yellow
		gfxDrawFontScaled(20, 86,  l4, 0xFFB0BEC5, hudScale); // Blue-grey
		gfxDrawFontScaled(20, 108, l5, 0xFF80D8FF, hudScale); // Light blue

		int curH = PIPE.GetHeight() > 0 ? PIPE.GetHeight() : h;
		int bottomY = curH - 55;
		if (bottomY < 140) bottomY = 140;
		gfxDrawFontScaled(20, bottomY,      "[LEFT/RIGHT] Prev/Next Model   [UP/DOWN] +/-10   [PGUP/PGDN] +/-50   [HOME/END] First/Last", 0xFF00E676, hudScale);
		gfxDrawFontScaled(20, bottomY + 22, "[SPACE] Orbit   [W] Wireframe   [L] Lighting   [MOUSE DRAG] Orbit   [WHEEL] Zoom", 0xFFCFD8DC, hudScale);

		ageEndFrame();
	} while (!ageExit());

	return 0;
}

// The shipped disc set is a handful of .dat archives sitting together; ASSETS.DAT is the
// marker to look for, because every layout of the set has it.
static bool rscHasShippedSet(const char *dir)
{
	if (!dir || !dir[0]) return false;
	char probe[512];
	size_t n = strlen(dir);
	const char *sep = (dir[n - 1] == '\\' || dir[n - 1] == '/') ? "" : "\\";
	formatf(probe, sizeof(probe), "%s%sASSETS.DAT", dir, sep);
	FILE *f = fopen(probe, "rb");
	if (!f) return false;
	fclose(f);
	return true;
}

// With no -path, find the disc set wherever it actually is: beside the executable first, then
// the working directory, so dropping rscview.exe into the assets folder is enough.  _pgmptr is
// the program path without dragging windows.h into this file.
static bool rscFindAssetDir(char *out, int size)
{
	if (_pgmptr && _pgmptr[0] && (int)strlen(_pgmptr) < size) {
		strncpy(out, _pgmptr, size - 1);
		out[size - 1] = 0;
		char *cut = strrchr(out, '\\');
		char *fwd = strrchr(out, '/');
		if (fwd > cut) cut = fwd;
		if (cut) {
			*cut = 0;
			if (rscHasShippedSet(out)) return true;
		}
	}
	if (rscHasShippedSet(".")) {
		strncpy(out, ".", size - 1);
		out[size - 1] = 0;
		return true;
	}
	return false;
}

int Main()
{
	const char *path = ".";
	// PC port: run against the disc set with no arguments at all when rscview sits in the
	// assets folder.  An explicit -path always wins, and finding nothing leaves "." in place.
	static char sAutoPath[512];
	if (!ARGS.Get("path") && rscFindAssetDir(sAutoPath, sizeof(sAutoPath)))
		path = sAutoPath;
	ARGS.Get("path", 0, &path);
	s_Wire = ARGS.Get("wire");
	ASSET.SetPath(path);
	memHeap::InitClass(path, 256, false);      // asset root; no gfx yet (SetRes first)
	// Mount the shipped archives so resources resolve out of ASSETS.DAT the way they do for the
	// game.  Only when the set is actually there (or -archive names one), so a loose asset tree
	// behaves, and reports, exactly as before.
	if (ARGS.Get("archive") || rscHasShippedSet(path))
		memHeap::Begin("ASSETS.DAT;TEXTURE.DAT;BANKS.DAT");

	const char *ptFile = 0, *ptNames = 0;
	if (ARGS.Get("pagetexfile", 0, &ptFile) && ptFile && ARGS.Get("pagetexname", 0, &ptNames) && ptNames) {
		// -pagetexfile resources/vehicle/decal_g -pagetexname ca_plate_1,cjrider_1: decode named page
		// textures the way the game looks them up (plates, rider skins); add -dumppagetex <dir>
		// to write them, -swizzletest to score the unswizzle, -pagetexswap/-pagetexnoswap to force it.
		PIPE.SetRes(640, 480, 32);
		PIPE.InitClass();
		const int slot = 5;
		if (!datPageFile::Mount(ptFile, slot)) {
			Errorf("rscview: could not mount '%s.ppf'", ptFile);
			return 1;
		}
		int decoded = 0, total = 0;
		if (!_stricmp(ptNames, "all")) {
			// -pagetexname all: every named texture in the file, for comparing a
			// decode across a whole page file rather than one suspect texture.
			int n = rscGetNumPageTextures(slot);
			for (int i = 0; i < n; i++) {
				const char *nm = rscGetPageTextureName(slot, i);
				if (!nm || !nm[0]) continue;
				total++;
				if (rscLoadPageTextureByName(slot, nm)) decoded++;
			}
		} else {
			char names[1024];
			strncpy(names, ptNames, sizeof(names) - 1);
			names[sizeof(names) - 1] = 0;
			for (char *tok = strtok(names, ","); tok; tok = strtok(0, ",")) {
				total++;
				if (rscLoadPageTextureByName(slot, tok)) decoded++;
			}
		}
		Displayf("rscview: -pagetexname decoded %d of %d from '%s.ppf'", decoded, total, ptFile);
		return decoded ? 0 : 1;
	}

	const char *pedArg = 0;
	if (ARGS.Get("ped", 0, &pedArg)) {
		// A bare -ped reads back as the switch after it: show the city's first ped then.
		if (pedArg && (!pedArg[0] || pedArg[0] == '-' || pedArg[0] == '/'))
			pedArg = 0;
		return RunPedViewer(pedArg);
	}
	const char *ambientArg = 0;
	if (ARGS.Get("ambient", 0, &ambientArg)) {
		// A bare -ambient reads back as the switch after it: show the civic then.
		if (!ambientArg || !ambientArg[0] || ambientArg[0] == '-' || ambientArg[0] == '/')
			ambientArg = "va_civic_sh";
		return RunAmbientViewer(ambientArg);
	}
	const char *datName = 0;
	if (ARGS.Get("dat", 0, &datName) && datName) {
		// e.g. -dat vp_lancer_04.dat: the per-car archive holding <car>.pck, <car>_o.pck and
		// the mod-part *.mesh.pck files; its entries then resolve through ASSET.Open.
		char datPath[512];
		ASSET.FullPath(datPath, sizeof(datPath), datName, "");
		size_t dl = strlen(datPath);
		if (dl && datPath[dl - 1] == '.') datPath[dl - 1] = 0;
		if (!zipFile::Mount(datPath)) Warningf("rscview: could not mount '%s'", datPath);
	}

	const char *pack = 0;
	const char *carArg = 0;
	bool isCar = ARGS.Get("car", 0, &carArg) && carArg && carArg[0];
	if (!isCar) isCar = ARGS.Get("car");
	const char *cityArg = 0;
	bool isCity = ARGS.Get("city", 0, &cityArg);
	bool isBrowse = ARGS.Get("browse") || isCity;
	char cityPackBuf[256] = {0};
	if (isCity) {
		ResolveCityPack(cityArg, cityPackBuf, sizeof(cityPackBuf));
		pack = cityPackBuf;
	}
	char carPackBuf[256] = {0};
	char carDatBuf[256] = {0};
	if (carArg && carArg[0]) {
		formatf(carPackBuf, sizeof(carPackBuf), "resources/vehicle/%s/%s_g", carArg, carArg);
		pack = carPackBuf;
		formatf(carDatBuf, sizeof(carDatBuf), "%s.dat", carArg);
		datName = carDatBuf;
		s_IsCarAssemble = true;
	} else if (ARGS.Get("assemble")) {
		s_IsCarAssemble = true;
	}

	if ((!ARGS.Get("pack", 0, &pack) || !pack) && !ARGS.Get("ppf") && !carArg && !isCity) {
		Errorf("rscview: -pack <name> or -city <name> or -car <name> is required (e.g. -city detroit, -car vp_lancer_04), or -ppf <name> -entry N");
		return 1;
	}
	if (!pack) pack = "page";
	s_PackName = pack;
	// The scene settings a race used to carry: city packs are named
	// <city>_<time of day>_<weather>, so the pack itself says which one this is.
	mcConfig::SetScene(strstr(pack, "_midnight") == 0, strstr(pack, "_rainy") != 0);

	if (strstr(pack, "vehicle/") && strstr(pack, "_g") && !ARGS.Get("model") && !ARGS.Get("models") && !ARGS.Get("all")) {
		s_IsCarAssemble = true;
		isCar = true;
	}

	if (ARGS.Get("dat", 0, &datName) && datName) {
		// e.g. -dat vp_lancer_04.dat: the per-car archive holding <car>.pck, <car>_o.pck and
		// the mod-part *.mesh.pck files; its entries then resolve through ASSET.Open.
		char datPath[512];
		ASSET.FullPath(datPath, sizeof(datPath), datName, "");
		size_t dl = strlen(datPath);
		if (dl && datPath[dl - 1] == '.') datPath[dl - 1] = 0;
		if (!zipFile::Mount(datPath)) Warningf("rscview: could not mount '%s'", datPath);
	} else if (carArg && carArg[0]) {
		char datPath[512];
		ASSET.FullPath(datPath, sizeof(datPath), carDatBuf, "");
		size_t dl = strlen(datPath);
		if (dl && datPath[dl - 1] == '.') datPath[dl - 1] = 0;
		if (zipFile::Mount(datPath)) Displayf("rscview: mounted '%s'", datPath);
	}

	u32 carPaintColor = mkrgba(30, 95, 215, 255); // Default Midnight Blue
	const char *colorArg = 0;
	if (ARGS.Get("carcolor", 0, &colorArg) && colorArg) {
		float cr = 0.0f, cg = 0.0f, cb = 0.0f;
		if (sscanf(colorArg, "%f,%f,%f", &cr, &cg, &cb) == 3) {
			carPaintColor = mkrgba((u8)(cr * 255.0f), (u8)(cg * 255.0f), (u8)(cb * 255.0f), 255);
		} else if (!_stricmp(colorArg, "red")) {
			carPaintColor = mkrgba(215, 30, 30, 255);
		} else if (!_stricmp(colorArg, "blue")) {
			carPaintColor = mkrgba(30, 95, 215, 255);
		} else if (!_stricmp(colorArg, "silver")) {
			carPaintColor = mkrgba(200, 205, 210, 255);
		} else if (!_stricmp(colorArg, "black")) {
			carPaintColor = mkrgba(35, 35, 38, 255);
		} else if (!_stricmp(colorArg, "yellow")) {
			carPaintColor = mkrgba(235, 195, 25, 255);
		} else if (!_stricmp(colorArg, "white")) {
			carPaintColor = mkrgba(235, 240, 245, 255);
		} else if (!_stricmp(colorArg, "green")) {
			carPaintColor = mkrgba(30, 160, 50, 255);
		} else if (!_stricmp(colorArg, "orange")) {
			carPaintColor = mkrgba(230, 100, 20, 255);
		}
	}

	datResourceImage image;
	const char *ppfName = 0;
	int ppfEntry = -1;
	if (ARGS.Get("ppf", 0, &ppfName) && ppfName && ARGS.Get("entry", -1, ppfEntry) && ppfEntry >= 0) {
		// A page out of a vehicle page file (resources/vehicle/rim|brake|exhaust|decal*.ppf).
		// Object pages carry their build address in the 32-byte page header
		// (rscLoadPageImage: the drwShaderModel is at payload +0).  Owner-less pages
		// (texture blocks) are loaded whole instead, with the build address recovered
		// by scoring candidate bases on how many pointers land on vtables / strings
		// (-pagebase overrides; -vif offsets then count from the page header).
		if (!datPageFile::Mount(ppfName, 1)) {
			Errorf("rscview: could not mount '%s.ppf'", ppfName);
			return 1;
		}
		int pageBaseArg = 0;
		ARGS.Get("pagebase", 0, pageBaseArg);
		u32 pageBase = (u32)pageBaseArg;
		char pageName[256];
		formatf(pageName, sizeof(pageName), "%s.ppf#%d", ppfName, ppfEntry);
		if (!pageBase && rscLoadPageImage(1, ppfEntry, image, pageName)) {
			Displayf("rscview: '%s.ppf' entry %d: %u byte object page built at %08x", ppfName, ppfEntry, image.GetSize(), image.GetBase());
			pageBase = image.GetBase();
			pack = pageName;
		} else {
		u32 pageSize = 0;
		static u8 pageBuf[1024 * 1024];
		if (!datPageFile::ReadEntry(1, ppfEntry, pageBuf, sizeof(pageBuf), &pageSize) || pageSize == 0 || pageSize > sizeof(pageBuf)) {
			Errorf("rscview: could not read entry %d of '%s.ppf'", ppfEntry, ppfName);
			return 1;
		}
		if (!pageBase) {
			const u32 *words = (const u32 *)pageBuf;
			u32 numWords = pageSize / 4;
			int bestScore = 0;
			for (u32 cand = 0x06800000; cand < 0x06900000; cand += 16) {
				int score = 0;
				for (u32 i = 0; i < numWords; i++) {
					u32 v = words[i];
					if (v < cand || v - cand + 4 > pageSize) continue;
					u32 off = v - cand;
					u32 target = words[off / 4];
					if (target >= 0x00700000 && target < 0x00800000) score += 2;
					else {
						const u8 *c = pageBuf + off;
						if (c[0] >= 32 && c[0] < 127 && c[1] >= 32 && c[1] < 127 && c[2] >= 32 && c[2] < 127) score += 1;
					}
				}
				if (score > bestScore) { bestScore = score; pageBase = cand; }
			}
			Displayf("rscview: '%s.ppf' entry %d: %u bytes, recovered build address %08x (score %d)", ppfName, ppfEntry, pageSize, pageBase, bestScore);
		}
		image.LoadFromMemory(pageName, pageBase, pageBuf, pageSize);
		pack = pageName;
		}
	} else if (!image.Load(pack)) {
		Errorf("rscview: could not load '%s.pck'", pack);
		return 1;
	}

	// Mount accompanying .ppf texture paging file if present
	if (!ppfName) {
		datPageFile::Mount(pack, 0);
		if (!datPageFile::IsMounted(0) || datPageFile::GetNumEntries(0) == 0) {
			char fallbackPpf[256];
			strncpy(fallbackPpf, pack, sizeof(fallbackPpf));
			fallbackPpf[sizeof(fallbackPpf) - 1] = 0;
			char *d = strstr(fallbackPpf, "_dawn_");
			if (!d) d = strstr(fallbackPpf, "_dusk_");
			if (!d) d = strstr(fallbackPpf, "_day_");
			if (d) {
				char rest[64];
				strncpy(rest, d + 5, sizeof(rest));
				strcpy(d, "_midnight");
				strcat(fallbackPpf, rest);
				datPageFile::Mount(fallbackPpf, 0);
			}
		}
	}
	if (ARGS.Get("dumpppf")) {
		if (datPageFile::IsMounted(0)) {
			int n = datPageFile::GetNumEntries(0);
			Displayf("rscview: PPF '%s.ppf' mounted with %d TOC entries, data @ 0x%08x",
				pack, n, datPageFile::GetDataStart(0));
			for (int e = 0; e < n && e < 16; e++) {
				Displayf("  entry %3d: sector %d (file offset 0x%08x)",
					e, datPageFile::GetEntrySector(0, e), datPageFile::GetEntrySector(0, e) * 2048);
			}
		} else {
			Warningf("rscview: no '%s.ppf' found to dump", pack);
		}
	}

	// City packs: decode shader group 0 so DrawModels can bind the page-file textures
	// (-notex draws the old flat shading).
	s_Image = &image;
	int shaderGroup = -1;          // -shadergroup N: bind one group's textures instead of merging all
	ARGS.Get("shadergroup", -1, shaderGroup);
	if (!ARGS.Get("notex") && !ppfName && rscDecodeCityShaders(image, shaderGroup, s_CityShaders) > 0) {
		s_CityTextures.Resize(s_CityShaders.GetCount());
		s_CityTexTried.Resize(s_CityShaders.GetCount());
		for (int i = 0; i < s_CityShaders.GetCount(); i++) { s_CityTextures[i] = 0; s_CityTexTried[i] = 0; }
		s_Textured = true;
		if (shaderGroup < 0)
			Displayf("rscview: %d city shaders (all groups merged); textures bind from the page file", s_CityShaders.GetCount());
		else
			Displayf("rscview: %d city shaders (group %d); textures bind from the page file", s_CityShaders.GetCount(), shaderGroup);
		if (rscDecodeShaderGroup(image, image.ReadU32(image.GetBase() + 0x10), s_SkyShaders) > 0) {
			s_SkyTextures.Resize(s_SkyShaders.GetCount());
			s_SkyTexTried.Resize(s_SkyShaders.GetCount());
			for (int i = 0; i < s_SkyShaders.GetCount(); i++) { s_SkyTextures[i] = 0; s_SkyTexTried[i] = 0; }
			rscFindSkyhatModels(image, s_SkyModels);
			Displayf("rscview: %d sky shaders, %d skyhat layers", s_SkyShaders.GetCount(), s_SkyModels.GetCount());
		}
		{
			std::unordered_map<u32, std::string> names;
			rscScanCityModelNames(image, names);
			Displayf("rscview: %d city models filed under a draw part (MAIN/GROUND/REFLECT/HDR/ALPHA)", (int)s_CityModelPart.size());
		}
		if (ARGS.Get("alltex")) {
			// -alltex: decode every city and sky shader texture up front (with -swizzletest / -texlog)
			int built = 0;
			for (int i = 0; i < s_CityShaders.GetCount(); i++) if (CityTextureFor(i)) built++;
			for (int i = 0; i < s_SkyShaders.GetCount(); i++) if (SkyTextureFor(i)) built++;
			Displayf("rscview: -alltex built %d textures", built);
		}
	}
	if (!ARGS.Get("notex") && ppfName && image.GetBase() && rscDecodeCarShaders(image, image.GetBase(), s_PageShaders) > 0) {
		s_CityTextures.Resize(s_PageShaders.GetCount());
		s_CityTexTried.Resize(s_PageShaders.GetCount());
		int withTex = 0;
		for (int i = 0; i < s_PageShaders.GetCount(); i++) { s_CityTextures[i] = 0; s_CityTexTried[i] = 0; if (s_PageShaders[i].numPageTextures) withTex++; }
		s_Textured = true;
		Displayf("rscview: %d page shaders, %d with page textures (e.g. [0] %s)", s_PageShaders.GetCount(), withTex, s_PageShaders[0].templateName);
		if (ARGS.Get("list")) {
			for (int i = 0; i < s_PageShaders.GetCount(); i++) {
				const rscShaderInfo &sh = s_PageShaders[i];
				for (int t = 0; t < sh.numPageTextures; t++) {
					const rscPageTexRef &r = sh.pageTextures[t];
					Displayf("  shader %2d %-40s page tex %d: entry %u +%#x %ux%u psm %#x%s", i, sh.templateName, t, r.entry, r.offset, r.width, r.height, r.psm,
					         r.psm != 0x13 ? " (PSMT4, not decoded)" : "");
				}
				if (sh.numPageTextures)
					Displayf("  shader %2d binds %s", i, CityTextureFor(i) ? CityTextureFor(i)->GetName() : "nothing");
			}
		}
	}
	if (ARGS.Get("loadcity"))
		return RunCityRender(pack);

	if (isBrowse) {
		return RunCityBrowser(pack, image);
	}

	u32 base = image.GetBase();
	u32 modelTypeAddr = (base && image.IsValidAddress(base + 8, 4)) ? image.ReadU32(base + 8) : 0;
	{
		int packWheelCount = (modelTypeAddr && image.IsValidAddress(modelTypeAddr + 0x3814, 4)) ? (int)image.ReadU32(modelTypeAddr + 0x3814) : 0;
		s_IsBike = (packWheelCount == 2);
		if (s_IsBike) Displayf("rscview: two wheel records -> motorcycle assembly");
	}
	u32 packModelAddr = 0;
	if (modelTypeAddr && image.IsValidAddress(modelTypeAddr + 0xd8, 4)) {
		packModelAddr = image.ReadU32(modelTypeAddr + 0xd8);
	}

	// Setup car shaders & materials
	carShaders.Reset();
	if (s_IsCarAssemble || isCar) {
		if (packModelAddr && image.IsValidAddress(packModelAddr, 0x20)) {
			rscDecodeCarShaders(image, packModelAddr, carShaders);
			s_CarMaterials.Resize(carShaders.GetCount());
			for (int i = 0; i < carShaders.GetCount(); i++) {
				const rscShaderInfo &sh = carShaders[i];
				rscViewerMatInfo &mat = s_CarMaterials[i];
				mat.color = 0;
				mat.alphaBlend = false;
				mat.texture = 0;
				const char *t = sh.templateName;
				const char *tname = (sh.numTextures > 0) ? sh.textures[0] : "";
				ClassifyMaterial(mat, t, tname, carPaintColor);
				bool allowDamage = ARGS.Get("damage");
				// Paint is a solid colour plus effect maps; a real texture in a carpaint shader's
				// list (a taillight or decal map on the Ram, Escalade, Supra...) is not its colour.
				bool paintTemplate = strstr(t, "carpaint") != 0;
				for (int texIdx = 0; texIdx < sh.numTextures && !paintTemplate; texIdx++) {
					const char *texName = sh.textures[texIdx];
					if (!texName || !texName[0]) continue;
					if (texName[0] == '_') {
						if (!strcmp(texName, "__licenseplate__") && strstr(t, "licenseplate")) {
							mat.texture = GetLicensePlateTexture();
							break;
						}
						if (!strcmp(texName, "__envmap__") && strstr(t, "chrome")) {
							mat.texture = GetCarEnvMapTexture();
							mat.isChrome = true;
							break;
						}
						continue;
					}
					if (!allowDamage && (strstr(t, "car_window") || strstr(t, "glass")) && strstr(texName, "_dmg")) {
						// The vehicle package exports 'car_window_blank_dmg' into the raw shader
						// struct so the damage system can blit crack zones at runtime upon collision.
						// Pristine cars in the garage and race use clean tinted glass without damage cracks.
						continue;
					}
					// per-car textures (the "<car>_trim" atlas) are resident in the pack
					gfxTexture *tex = rscLoadResidentTexture(image, texName);
					if (!tex) tex = gfxGetTexture(texName, true, false);
					if (tex) {
						mat.texture = tex;
						// a cutout placeholder (alpha 0) becomes a white modulate once its image binds
						if (((mat.color >> 24) & 0xff) == 0) mat.color |= 0xff000000u;
						break;
					}
				}
				if (strstr(t, "chrome")) {
					mat.isChrome = true;
					if (!mat.texture) mat.texture = GetCarEnvMapTexture();
				} else if (strstr(t, "licenseplate")) {
					if (!mat.texture) mat.texture = GetLicensePlateTexture();
				}
				if (!mat.texture && strstr(t, "lit_textured")) {
					// The per-car trim atlas (<car>_trim) is not on disc as a loose texture; the
					// hood / bumper trim it covers is painted bodywork, so fall back to the paint.
					mat.color = carPaintColor;
					SetMatShading(mat, 0.7f, 48.0f, 0.25f);
				}
				char texList[6 * 49 + 8] = "";
				for (int texIdx = 0; texIdx < sh.numTextures; texIdx++) {
					if (texIdx) strcat(texList, ",");
					strncat(texList, sh.textures[texIdx], 48);
				}
				Displayf("  car shader %2d: '%s' [%s] -> tex '%s', color 0x%08x%s",
				         i, sh.templateName, texList, mat.texture ? mat.texture->GetName() : "none", mat.color,
				         mat.isChrome ? " [chrome]" : "");
			}
			Displayf("rscview: %d car shaders configured with materials/textures", carShaders.GetCount());
		}
	}

	atArray<u32> addrs;
	int vifOff = -1;
	bool rawVif = ARGS.Get("vif", -1, vifOff) && vifOff >= 0;
	int vt = isCar ? rscCarModelVTable : rscModelVTable;
	ARGS.Get("vtable", vt, vt);
	if (isCar || vt == rscCarModelVTable) {
		rscFindCarModels(image, addrs);
		isCar = true;
	} else {
		rscFindModels(image, addrs, (u32)vt);
		if (!addrs.GetCount()) {
			rscFindCarModels(image, addrs);
			if (addrs.GetCount()) isCar = true;
		}
	}
	if (rawVif) {
		// e.g. a tire.ppf page: no objects at all, the payload is the VIF stream itself
		addrs.Reset();
		addrs.Append(image.GetBase() + (u32)vifOff);
		isCar = true;
	}

	if (s_IsCarAssemble) {
		s_Models.Reset();
		// The pack's skeleton: parts are exported bone-local, and a crBoneData carries an
		// absolute rest offset (+0x00) AND a locked rest rotation (+0x2c, eulers; dofs bit 9).
		// Translating by the offset alone leaves e.g. a bike's fork_joint (rot 0,pi,0) facing
		// backwards and the steer bone without its rake.
		int numBones = rscDecodeSkeleton(image, packModelAddr, s_Bones);
		s_HaveKits = !ARGS.Get("nameparts") && rscDecodeCarKits(image, modelTypeAddr, s_Bones, s_Kits) && s_Kits.numFamiliesFound > 0;
		if (s_HaveKits) {
			int nv = 0, ns = 0;
			for (int i = 0; i < s_Bones.GetCount(); i++) { nv += s_Kits.variantBone[i]; ns += s_Kits.stockBone[i]; }
			Displayf("rscview assemble: kit manifest: %d families, %d stock bones, %d variant bones, drop shadow bone %d",
			         s_Kits.numFamiliesFound, ns, nv, s_Kits.dropShadowBone);
		} else {
			Displayf("rscview assemble: no kit manifest in the pack, selecting stock parts by name");
		}
		Displayf("rscview assemble: loaded %d skeleton bones for part placement%s", numBones, ARGS.Get("nobones") ? " (ignored: -nobones)" : "");
		if (ARGS.Get("bonelog"))
			for (int i = 0; i < numBones; i++)
				Displayf("  bone %3d %-24s parent %3d off (%.3f %.3f %.3f) rot (%.3f %.3f %.3f) dofs %#x", i, s_Bones[i].name, s_Bones[i].parent,
				         s_Bones[i].offset.x, s_Bones[i].offset.y, s_Bones[i].offset.z, s_Bones[i].rotation.x, s_Bones[i].rotation.y, s_Bones[i].rotation.z, s_Bones[i].dofs);

		if (packModelAddr && image.IsValidAddress(packModelAddr, 0x20)) {
			u32 tblAddr = image.ReadU32(packModelAddr + 0x10); // LOD 0 table
			if (tblAddr && image.IsValidAddress(tblAddr, 0x14)) {
				u16 count = image.ReadU16(tblAddr + 2);
				u32 nodesPtr = image.ReadU32(tblAddr + 8);
				u32 namesPtr = image.ReadU32(tblAddr + 12);
				if (count > 0 && count <= 512 && nodesPtr && namesPtr) {
					for (int i = 0; i < (int)count; i++) {
						u32 nodeAddr = image.ReadU32(nodesPtr + (u32)i * 4);
						u32 nameAddr = image.ReadU32(namesPtr + (u32)i * 4);
						if (!nodeAddr) continue;
						const char *name = nameAddr ? image.ReadString(nameAddr) : "";
						char lowerName[128];
						strncpy(lowerName, name, sizeof(lowerName) - 1);
						lowerName[sizeof(lowerName) - 1] = 0;
						for (char *c = lowerName; *c; c++) *c = (char)tolower(*c);

						// the stock car = every LOD0 node that does not hang off a kit variant bone
						// (rmcCarModelType's manifest); the name rule only for packs without one
						bool includePart;
						if (s_HaveKits) {
							int nodeBone = (int)image.ReadU16(nodeAddr + 6);
							includePart = !sIsLodOrEffect(lowerName) && !rscIsKitVariantBone(s_Kits, s_Bones, nodeBone);
						} else {
							includePart = sIsStockCarPart(lowerName);
						}
						if (s_IsBike && !s_HaveKits) {
							// bikes without a manifest: every part except the medium LODs (*_m, shock_m_*), the low LOD and the neon
							size_t ln = strlen(lowerName);
							bool mediumLod = (ln > 2 && strcmp(lowerName + ln - 2, "_m") == 0) || strstr(lowerName, "_m_") != 0 || strstr(lowerName, "_m.") != 0;
							includePart = !mediumLod && !strstr(lowerName, "llod") && !strstr(lowerName, "neonglow");
						}
						// the trunk logo decal ("*_decal_tk") is a bone-placed quad: at the part origin it floats under the car
						// in theory, bone translation now places the trunk badges on the read of the trunk, so we can include them
						// if (includePart && strstr(lowerName, "decal_tk")) includePart = false;
						if (includePart) {
							rscModel m;
							if (rscDecodeCarModel(image, nodeAddr, m)) {
								m.isTaillight = (strstr(lowerName, "tl_") != 0 || strstr(lowerName, "tail") != 0);
								m.isHeadlight = (strstr(lowerName, "hl_") != 0 || strstr(lowerName, "headlight") != 0 || strstr(lowerName, "headlamp") != 0);

								// Bone-local part -> model space: R(locked rotation) * v + offset
								if (!ARGS.Get("nobones") && m.boneIndex > 0 && m.boneIndex < s_Bones.GetCount())
									rscPlaceModelOnBone(m, s_Bones[m.boneIndex]);

								if (strstr(lowerName, "plate_h")) {
									for (int g = 0; g < m.geometries.GetCount(); g++) {
										for (int bb = 0; bb < m.geometries[g].batches.GetCount(); bb++) {
											rscGeomBatch &batch = m.geometries[g].batches[bb];
											if (batch.verts.GetCount() == 4 && batch.uvs.GetCount() >= 4) {
												float minX = batch.verts[0].x, maxX = batch.verts[0].x;
												float minY = batch.verts[0].y, maxY = batch.verts[0].y;
												for (int v = 1; v < 4; v++) {
													if (batch.verts[v].x < minX) minX = batch.verts[v].x;
													if (batch.verts[v].x > maxX) maxX = batch.verts[v].x;
													if (batch.verts[v].y < minY) minY = batch.verts[v].y;
													if (batch.verts[v].y > maxY) maxY = batch.verts[v].y;
												}
												float dx = (maxX - minX > 1e-4f) ? (maxX - minX) : 1.0f;
												float dy = (maxY - minY > 1e-4f) ? (maxY - minY) : 1.0f;
												for (int v = 0; v < 4; v++) {
													batch.uvs[v].x = (batch.verts[v].x - minX) / dx;
													batch.uvs[v].y = (maxY - batch.verts[v].y) / dy;
												}
											}
										}
									}
								}

								s_Models.Append(m);
								Displayf("rscview assemble: attached [%d] %s (%d geoms, %d verts)",
								         i, name, m.geometries.GetCount(), m.numVerts);
								for (int g = 0; g < m.geometries.GetCount(); g++) {
									int sh = m.geometries[g].shaderIdx;
									const char *shTmpl = (sh >= 0 && sh < carShaders.GetCount()) ? carShaders[sh].templateName : "none";
									const char *tname = (sh >= 0 && sh < carShaders.GetCount() && carShaders[sh].numTextures > 0) ? carShaders[sh].textures[0] : "";
									if (ARGS.Get("geomboxes")) {
										// Local-space extent of the geom, so it can be said with
										// certainty whether one geom sits behind another - e.g.
										// whether a headlight's chrome reflector really is behind
										// the emissive lens element that fades over it.
										Vector3 lo( 1e30f,  1e30f,  1e30f);
										Vector3 hi(-1e30f, -1e30f, -1e30f);
										int nv = 0;
										for (int bb = 0; bb < m.geometries[g].batches.GetCount(); bb++) {
											const rscGeomBatch &batch = m.geometries[g].batches[bb];
											for (int v = 0; v < batch.verts.GetCount(); v++) {
												const Vector3 &p = batch.verts[v];
												if (p.x < lo.x) lo.x = p.x;  if (p.x > hi.x) hi.x = p.x;
												if (p.y < lo.y) lo.y = p.y;  if (p.y > hi.y) hi.y = p.y;
												if (p.z < lo.z) lo.z = p.z;  if (p.z > hi.z) hi.z = p.z;
												nv++;
											}
										}
										Displayf("    geom %d: sh %d ('%s', tex '%s') %d verts box x[%.3f %.3f] y[%.3f %.3f] z[%.3f %.3f]",
										         g, sh, shTmpl, tname, nv, lo.x, hi.x, lo.y, hi.y, lo.z, hi.z);
										continue;
									}
									Displayf("    geom %d: sh %d ('%s', tex '%s')", g, sh, shTmpl, tname);
								}
							}
						}
					}
				}
			}
		}

		// Load tire from resources/vehicle/tire.ppf
		int tireEntry = s_IsBike ? 11 : 5; // Profile 5 matches low-profile sports tire; 11 is streetbike tire
		ARGS.Get("tire", tireEntry, tireEntry);
		atArray<rscModel> tireBaseModels;
		if (datPageFile::Mount("resources/vehicle/tire", 2)) {
			datResourceImage tireImage;
			char tirePageName[64];
			formatf(tirePageName, sizeof(tirePageName), "tire_%d", tireEntry);
			if (rscLoadPageImage(2, tireEntry, tireImage, tirePageName)) {
				atArray<u32> tireAddrs;
				rscFindCarModels(tireImage, tireAddrs);
				for (int t = 0; t < tireAddrs.GetCount(); t++) {
					rscModel tm;
					if (rscDecodeCarModel(tireImage, tireAddrs[t], tm)) {
						tireBaseModels.Append(tm);
						break; // High-LOD tire model
					}
				}
				Displayf("rscview assemble: loaded tire model %d (found %d models, %d verts)",
				         tireEntry, tireBaseModels.GetCount(), tireBaseModels.GetCount() ? tireBaseModels[0].numVerts : 0);
			} else {
				Warningf("rscview: could not load tire entry %d from 'resources/vehicle/tire.ppf'", tireEntry);
			}
		}

		// Load wheels from resources/vehicle/rim.ppf
		int rimEntry = s_IsBike ? 89 : 167; // Generic authentic wheel mesh; 89 is bike rim
		ARGS.Get("rim", rimEntry, rimEntry);
		if (datPageFile::Mount("resources/vehicle/rim", 1)) {
			datResourceImage rimImage;
			char pageName[64];
			formatf(pageName, sizeof(pageName), "rim_%d", rimEntry);
			if (rscLoadPageImage(1, rimEntry, rimImage, pageName)) {
				atArray<rscShaderInfo> rimShaders;
				rscDecodeCarShaders(rimImage, rimImage.GetBase(), rimShaders);
				s_RimMaterials.Reset();
				gfxTexture *rimFaceTex = 0;
				for (int s = 0; s < rimShaders.GetCount(); s++) {
					const rscShaderInfo &sh = rimShaders[s];
					char rtexName[64];
					formatf(rtexName, sizeof(rtexName), "rscview_rimface_%d", s);
					for (int t = 0; t < sh.numPageTextures && !rimFaceTex; t++) {
						rimFaceTex = rscLoadPageTexture(1, sh.pageTextures[t], rtexName);
					}
				}

				// Material 0: tire rubber
				rscViewerMatInfo matTire = {};
				matTire.color = mkrgba(26, 26, 29, 255);
				SetMatShading(matTire, 0.12f, 10.0f, 0.03f);
				s_RimMaterials.Append(matTire);

				// Material 1: rim lip / barrel (polished alloy)
				rscViewerMatInfo matLip = {};
				matLip.color = mkrgba(220, 225, 235, 255);
				matLip.texture = GetCarEnvMapTexture();
				SetMatShading(matLip, 1.4f, 110.0f, 0.95f, true, false, false, matLip.texture != NULL);
				s_RimMaterials.Append(matLip);

				// Material 2: caliper + rotor
				rscViewerMatInfo matCaliper = {};
				matCaliper.color = mkrgba(200, 30, 25, 255);
				SetMatShading(matCaliper, 0.6f, 32.0f, 0.1f);
				s_RimMaterials.Append(matCaliper);

				// Material 3: wheel face: chrome spokes under the page's overlay texture
				// (mostly transparent between the spokes, so the barrel / brake show through)
				rscViewerMatInfo matFace = {};
				matFace.color = mkrgba(225, 230, 238, 255);
				matFace.texture = rimFaceTex;
				matFace.alphaBlend = rimFaceTex && rimFaceTex->HasAlpha();
				SetMatShading(matFace, 1.4f, 110.0f, 0.9f, true);
				s_RimMaterials.Append(matFace);
				Displayf("rscview: rim face texture %s", rimFaceTex ? rimFaceTex->GetName() : "none");

				// Material 4: brake rotor (dull steel)
				rscViewerMatInfo matRotor = {};
				matRotor.color = mkrgba(120, 122, 126, 255);
				SetMatShading(matRotor, 0.6f, 40.0f, 0.25f, true);
				s_RimMaterials.Append(matRotor);

				// Brake caliper + rotor from resources/vehicle/brake.ppf (entry 1: caliper model 0, rotor quad model 1)
				int brakeEntry = 1;
				ARGS.Get("brake", brakeEntry, brakeEntry);
				atArray<rscModel> brakeBaseModels;
				if (datPageFile::Mount("resources/vehicle/brake", 3)) {
					datResourceImage brakeImage;
					char brakeName[64];
					formatf(brakeName, sizeof(brakeName), "brake_%d", brakeEntry);
					if (rscLoadPageImage(3, brakeEntry, brakeImage, brakeName)) {
						atArray<u32> brakeAddrs;
						rscFindCarModels(brakeImage, brakeAddrs);
						for (int b = 0; b < brakeAddrs.GetCount() && b < 2; b++) {
							rscModel bm;
							if (rscDecodeCarModel(brakeImage, brakeAddrs[b], bm)) brakeBaseModels.Append(bm);
						}
						Displayf("rscview assemble: loaded brake entry %d (%d models)", brakeEntry, brakeBaseModels.GetCount());
					}
				}

				atArray<u32> rimAddrs;
				rscFindCarModels(rimImage, rimAddrs);
				atArray<rscModel> rimBaseModels;
				for (int r = 0; r < rimAddrs.GetCount(); r++) {
					rscModel rm;
					if (rscDecodeCarModel(rimImage, rimAddrs[r], rm))
						rimBaseModels.Append(rm);
				}

				Vector3 wheelOffsets[4];
				int numWheelsFound = 0;
				u32 packWheelsAddr = (modelTypeAddr && image.IsValidAddress(modelTypeAddr + 0x3810, 8)) ? image.ReadU32(modelTypeAddr + 0x3810) : 0;
				int nPackWheels = (modelTypeAddr && image.IsValidAddress(modelTypeAddr + 0x3814, 4)) ? (int)image.ReadU32(modelTypeAddr + 0x3814) : 0;
				if (nPackWheels >= 2 && packWheelsAddr && image.IsValidAddress(packWheelsAddr, (u32)nPackWheels * 28)) {
					for (int w = 0; w < nPackWheels && w < 4; w++) {
						u32 wAddr = packWheelsAddr + (u32)w * 28;
						float ox = image.ReadFloat(wAddr + 16);
						float oy = image.ReadFloat(wAddr + 20);
						float oz = image.ReadFloat(wAddr + 24);
						wheelOffsets[w] = Vector3(ox, oy, oz);
						numWheelsFound++;
					}
				} else {
					wheelOffsets[0] = Vector3(-0.74f, 0.32f, -1.39f);
					wheelOffsets[1] = Vector3( 0.74f, 0.32f, -1.39f);
					wheelOffsets[2] = Vector3(-0.74f, 0.32f,  1.38f);
					wheelOffsets[3] = Vector3( 0.74f, 0.32f,  1.38f);
					numWheelsFound = 4;
				}

				for (int w = 0; w < numWheelsFound; w++) {
					const Vector3 &wPos = wheelOffsets[w];
					bool isRightSide = (wPos.x > 0.001f);
					float wheelScale = (wPos.y > 0.1f) ? (wPos.y / 0.50f) : 0.64f;

					// 1. Attach 3D Tire geometry from tire.ppf
					if (tireBaseModels.GetCount() > 0) {
						const rscModel &srcTire = tireBaseModels[0];
						rscModel instTire;
						instTire.addr = srcTire.addr;
						instTire.numVerts = srcTire.numVerts;
						instTire.boxMin = Vector3(1e30f, 1e30f, 1e30f);
						instTire.boxMax = Vector3(-1e30f, -1e30f, -1e30f);

						for (int g = 0; g < srcTire.geometries.GetCount(); g++) {
							rscGeometry gInst = srcTire.geometries[g];
							gInst.shaderIdx = 1000; // Tire rubber

							for (int b = 0; b < gInst.batches.GetCount(); b++) {
								rscGeomBatch &batch = gInst.batches[b];
								for (int v = 0; v < batch.verts.GetCount(); v++) {
									Vector3 p = batch.verts[v];
									float tireWidthScale = s_IsBike ? 0.45f : 0.73f;
									p.x *= (wheelScale * tireWidthScale);
									p.y *= wheelScale;
									p.z *= wheelScale;
									if (isRightSide) {
										p.x = -p.x;
										p.z = -p.z;
									}
									p.x += wPos.x;
									p.y += wPos.y;
									p.z += wPos.z;
									batch.verts[v] = p;
									if (p.x < instTire.boxMin.x) instTire.boxMin.x = p.x;
									if (p.x > instTire.boxMax.x) instTire.boxMax.x = p.x;
									if (p.y < instTire.boxMin.y) instTire.boxMin.y = p.y;
									if (p.y > instTire.boxMax.y) instTire.boxMax.y = p.y;
									if (p.z < instTire.boxMin.z) instTire.boxMin.z = p.z;
									if (p.z > instTire.boxMax.z) instTire.boxMax.z = p.z;
								}
							}
							instTire.geometries.Append(gInst);
						}
						s_Models.Append(instTire);
					}

					// 2. Attach Rim, Caliper, Face models from rim.ppf
					float rimRadScale = (tireBaseModels.GetCount() > 0) ? (s_IsBike ? 0.80f : 0.70f) : 1.0f;    // lip just inside the tire bead
					float rimXShift = (tireBaseModels.GetCount() > 0) ? (s_IsBike ? -0.06f : 0.0f) : 0.0f;     // centered with tire on cars
					// rim page: model 0 = spokes + barrel (geometry 1 = its own tread band), model 1 = the
					// spinner riding on the face, model 2 = the flat overlay disc used by texture-only rims
					int maxModelsToAttach = rimBaseModels.GetCount() > 2 ? 2 : rimBaseModels.GetCount();
					if (ARGS.Get("rimdisc")) maxModelsToAttach = rimBaseModels.GetCount() > 3 ? 3 : rimBaseModels.GetCount();
					if (s_IsBike && maxModelsToAttach > 1) maxModelsToAttach = 1;     // no Davin spinner on a bike
					// a bike wheel sits on the centre line: attach the rim twice, mirrored, so both sides have spokes
					for (int side = 0; side < (s_IsBike ? 2 : 1); side++)
					for (int r = 0; r < maxModelsToAttach; r++) {
						bool mirrorRim = s_IsBike ? (side == 1) : isRightSide;
						const rscModel &srcMdl = rimBaseModels[r];
						rscModel instMdl;
						instMdl.addr = srcMdl.addr;
						instMdl.numVerts = srcMdl.numVerts;
						instMdl.boxMin = Vector3(1e30f, 1e30f, 1e30f);
						instMdl.boxMax = Vector3(-1e30f, -1e30f, -1e30f);

						for (int g = 0; g < srcMdl.geometries.GetCount(); g++) {
							// If authentic tire is attached, skip Model 0 Geom 1 (the generic cylinder barrel from rim.ppf)
							if (tireBaseModels.GetCount() > 0 && r == 0 && g == 1) continue;

							rscGeometry gInst = srcMdl.geometries[g];
							// 1001 = rim (spokes + barrel), 1003 = chrome spinner / face disc, 1000 = tread band
							if (r == 0) {
								if (tireBaseModels.GetCount() == 0 && g == 1)
									gInst.shaderIdx = 1000;
								else
									gInst.shaderIdx = 1001;
							} else {
								gInst.shaderIdx = 1003;
							}

							for (int b = 0; b < gInst.batches.GetCount(); b++) {
								rscGeomBatch &batch = gInst.batches[b];
								for (int v = 0; v < batch.verts.GetCount(); v++) {
									Vector3 p = batch.verts[v];
									p.x = (p.x + rimXShift) * wheelScale;
									p.y *= (wheelScale * rimRadScale);
									p.z *= (wheelScale * rimRadScale);
									if (mirrorRim) {
										p.x = -p.x;
										p.z = -p.z;
									}
									p.x += wPos.x;
									p.y += wPos.y;
									p.z += wPos.z;
									batch.verts[v] = p;
									if (p.x < instMdl.boxMin.x) instMdl.boxMin.x = p.x;
									if (p.x > instMdl.boxMax.x) instMdl.boxMax.x = p.x;
									if (p.y < instMdl.boxMin.y) instMdl.boxMin.y = p.y;
									if (p.y > instMdl.boxMax.y) instMdl.boxMax.y = p.y;
									if (p.z < instMdl.boxMin.z) instMdl.boxMin.z = p.z;
									if (p.z > instMdl.boxMax.z) instMdl.boxMax.z = p.z;
								}
							}
							instMdl.geometries.Append(gInst);
						}
						s_Models.Append(instMdl);
					}
					// 3. Brake caliper (1002) + rotor (1004), tucked inside the barrel behind the spokes
					float brakeXShift = rimXShift + 0.16f;      // +x in rim space is inboard: rotor behind the spokes
					const char *bxs = 0;
					if (ARGS.Get("brakex", 0, &bxs) && bxs) brakeXShift = (float)atof(bxs);
					for (int b = 0; !s_IsBike && b < brakeBaseModels.GetCount(); b++) {
						const rscModel &srcB = brakeBaseModels[b];
						rscModel instB;
						instB.addr = srcB.addr;
						instB.numVerts = srcB.numVerts;
						instB.boxMin = Vector3(1e30f, 1e30f, 1e30f);
						instB.boxMax = Vector3(-1e30f, -1e30f, -1e30f);
						for (int g = 0; g < srcB.geometries.GetCount(); g++) {
							rscGeometry gInst = srcB.geometries[g];
							gInst.shaderIdx = (b == 0) ? 1002 : 1004;
							for (int bb = 0; bb < gInst.batches.GetCount(); bb++) {
								rscGeomBatch &batch = gInst.batches[bb];
								for (int v = 0; v < batch.verts.GetCount(); v++) {
									Vector3 p = batch.verts[v];
									p.x = (p.x + brakeXShift) * wheelScale;
									p.y *= (wheelScale * rimRadScale);
									p.z *= (wheelScale * rimRadScale);
									if (isRightSide) { p.x = -p.x; p.z = -p.z; }
									p.x += wPos.x; p.y += wPos.y; p.z += wPos.z;
									batch.verts[v] = p;
									if (p.x < instB.boxMin.x) instB.boxMin.x = p.x; if (p.x > instB.boxMax.x) instB.boxMax.x = p.x;
									if (p.y < instB.boxMin.y) instB.boxMin.y = p.y; if (p.y > instB.boxMax.y) instB.boxMax.y = p.y;
									if (p.z < instB.boxMin.z) instB.boxMin.z = p.z; if (p.z > instB.boxMax.z) instB.boxMax.z = p.z;
								}
							}
							instB.geometries.Append(gInst);
						}
						s_Models.Append(instB);
					}

					Displayf("rscview assemble: attached wheel %d at (%.2f, %.2f, %.2f)%s (scale %.2f)",
					         w, wPos.x, wPos.y, wPos.z, isRightSide ? " [mirrored]" : "", wheelScale);
				}
			} else {
				Warningf("rscview: could not load wheel entry %d from 'resources/vehicle/rim.ppf'", rimEntry);
			}
		}

		s_Min = Vector3(1e30f, 1e30f, 1e30f);
		s_Max = Vector3(-1e30f, -1e30f, -1e30f);
		int totalVerts = 0, totalBatches = 0;
		for (int m = 0; m < s_Models.GetCount(); m++) {
			const rscModel &model = s_Models[m];
			totalVerts += model.numVerts;
			for (int g = 0; g < model.geometries.GetCount(); g++)
				totalBatches += model.geometries[g].batches.GetCount();
			if (model.boxMin.x < s_Min.x) s_Min.x = model.boxMin.x; if (model.boxMax.x > s_Max.x) s_Max.x = model.boxMax.x;
			if (model.boxMin.y < s_Min.y) s_Min.y = model.boxMin.y; if (model.boxMax.y > s_Max.y) s_Max.y = model.boxMax.y;
			if (model.boxMin.z < s_Min.z) s_Min.z = model.boxMin.z; if (model.boxMax.z > s_Max.z) s_Max.z = model.boxMax.z;
		}
		Displayf("rscview: assembled vehicle with %d models (%d verts, %d batches), box (%.1f %.1f %.1f)-(%.1f %.1f %.1f)",
		         s_Models.GetCount(), totalVerts, totalBatches, s_Min.x, s_Min.y, s_Min.z, s_Max.x, s_Max.y, s_Max.z);
	} else {
		Displayf("rscview: %d %s objects in '%s'", addrs.GetCount(), rawVif ? "raw VIF stream" : isCar ? "carModel" : "rmcModel", pack);
		if (!addrs.GetCount()) return 1;

		int first = 0, count = addrs.GetCount();
		if (ARGS.Get("all")) {
			first = 0;
			count = addrs.GetCount();
		}
		int one = -1;
		if (ARGS.Get("model", -1, one) && one >= 0) { first = one; count = 1; }
		const char *fs = 0;
		if (ARGS.Get("models", 0, &fs) && fs) {
			first = atoi(fs);
			count = 1;
			for (int a = 1; a < ARGS.Argc - 2; a++) {
				if (!_stricmp(ARGS.Argv[a], "-models") || !_stricmp(ARGS.Argv[a], "/models")) {
					if (ARGS.Argv[a + 2][0] != '-' && ARGS.Argv[a + 2][0] != '/')
						count = atoi(ARGS.Argv[a + 2]);
				}
			}
		}

		if (first < 0 || first >= addrs.GetCount()) {
			Errorf("rscview: model index %d out of range (valid: 0..%d)", first, addrs.GetCount() - 1);
			return 1;
		}
		if (first + count > addrs.GetCount()) count = addrs.GetCount() - first;
		ARGS.Get("part", -1, s_Part);
		s_Wire = ARGS.Get("wire");
		bool list = ARGS.Get("list");
		const char *primName = 0;
		if (ARGS.Get("prim", 0, &primName) && primName)
			s_Prim = !strcmp(primName, "list") ? 1 : !strcmp(primName, "fan") ? 2 : !strcmp(primName, "quad") ? 3 : 0;

		s_Min = Vector3(1e30f, 1e30f, 1e30f);
		s_Max = Vector3(-1e30f, -1e30f, -1e30f);
		int totalVerts = 0, totalBatches = 0;
		for (int i = 0; i < count; i++) {
			rscModel model;
			bool ok;
			if (rawVif) {
				model.addr = addrs[first + i];
				model.geometries.Reset();
				model.numVerts = 0;
				model.boxMin = Vector3(1e30f, 1e30f, 1e30f);
				model.boxMax = Vector3(-1e30f, -1e30f, -1e30f);
				rscGeometry g;
				g.part = 0; g.index = 0; g.addr = model.addr; g.declaredVerts = 0; g.shaderIdx = 0;
				rscDecodeVifStream(image, model.addr, image.GetBase() + image.GetSize(), g.batches);
				for (int b = 0; b < g.batches.GetCount(); b++)
					for (int v = 0; v < g.batches[b].verts.GetCount(); v++) {
						const Vector3 &p = g.batches[b].verts[v];
						if (p.x < model.boxMin.x) model.boxMin.x = p.x; if (p.x > model.boxMax.x) model.boxMax.x = p.x;
						if (p.y < model.boxMin.y) model.boxMin.y = p.y; if (p.y > model.boxMax.y) model.boxMax.y = p.y;
						if (p.z < model.boxMin.z) model.boxMin.z = p.z; if (p.z > model.boxMax.z) model.boxMax.z = p.z;
						model.numVerts++;
					}
				model.geometries.Append(g);
				ok = model.numVerts > 0;
			} else {
				ok = isCar ? rscDecodeCarModel(image, addrs[first + i], model)
				           : rscDecodeModel(image, addrs[first + i], model);
			}
			if (!ok) continue;
			int batches = 0;

			for (int g = 0; g < model.geometries.GetCount(); g++) batches += model.geometries[g].batches.GetCount();
			if (list) {
				float umin = 1e30f, umax = -1e30f, vmin = 1e30f, vmax = -1e30f; int shmin = 1 << 30, shmax = -1;
				for (int g = 0; g < model.geometries.GetCount(); g++) {
					const rscGeometry &ge = model.geometries[g];
					if (ge.shaderIdx < shmin) shmin = ge.shaderIdx; if (ge.shaderIdx > shmax) shmax = ge.shaderIdx;
					for (int b = 0; b < ge.batches.GetCount(); b++)
						for (int v = 0; v < ge.batches[b].uvs.GetCount(); v++) {
							const Vector2 &uv = ge.batches[b].uvs[v];
							if (uv.x < umin) umin = uv.x; if (uv.x > umax) umax = uv.x; if (uv.y < vmin) vmin = uv.y; if (uv.y > vmax) vmax = uv.y;
						}
				}
				Displayf("  model %4d @%08x: %d geometries, %d batches, %d verts, box (%.1f %.1f %.1f)-(%.1f %.1f %.1f) uv u[%.2f..%.2f] v[%.2f..%.2f] shader %d..%d",
					first + i, model.addr, model.geometries.GetCount(), batches, model.numVerts,
					model.boxMin.x, model.boxMin.y, model.boxMin.z, model.boxMax.x, model.boxMax.y, model.boxMax.z, umin, umax, vmin, vmax, shmin, shmax);
				if (ARGS.Get("geoms")) {
					for (int g = 0; g < model.geometries.GetCount(); g++) {
						const rscGeometry &ge = model.geometries[g];
						float xmin = 1e30f, xmax = -1e30f, rmin = 1e30f, rmax = 0.0f, gu0 = 1e30f, gu1 = -1e30f, gv0 = 1e30f, gv1 = -1e30f; int nv = 0;
						for (int b = 0; b < ge.batches.GetCount(); b++) {
							const rscGeomBatch &bt = ge.batches[b];
							for (int v = 0; v < bt.verts.GetCount(); v++) {
								const Vector3 &p = bt.verts[v];
								float r = sqrtf(p.y * p.y + p.z * p.z);
								if (p.x < xmin) xmin = p.x; if (p.x > xmax) xmax = p.x; if (r < rmin) rmin = r; if (r > rmax) rmax = r;
								nv++;
							}
							for (int v = 0; v < bt.uvs.GetCount(); v++) {
								const Vector2 &uv = bt.uvs[v];
								if (uv.x < gu0) gu0 = uv.x; if (uv.x > gu1) gu1 = uv.x; if (uv.y < gv0) gv0 = uv.y; if (uv.y > gv1) gv1 = uv.y;
							}
						}
						Displayf("      geometry %d: shader %d, %d batches, %d verts (declared %d)%s, x [%.2f..%.2f], radius [%.2f..%.2f], uv u[%.2f..%.2f] v[%.2f..%.2f]",
						         g, ge.shaderIdx, ge.batches.GetCount(), nv, ge.declaredVerts, nv != ge.declaredVerts ? " MISMATCH" : "", xmin, xmax, rmin, rmax, gu0, gu1, gv0, gv1);
					}
				}
			}
			totalVerts += model.numVerts;
			totalBatches += batches;
			if (model.boxMin.x < s_Min.x) s_Min.x = model.boxMin.x; if (model.boxMax.x > s_Max.x) s_Max.x = model.boxMax.x;
			if (model.boxMin.y < s_Min.y) s_Min.y = model.boxMin.y; if (model.boxMax.y > s_Max.y) s_Max.y = model.boxMax.y;
			if (model.boxMin.z < s_Min.z) s_Min.z = model.boxMin.z; if (model.boxMax.z > s_Max.z) s_Max.z = model.boxMax.z;
			s_Models.Append(model);
		}
		Displayf("rscview: decoded %d models, %d batches, %d vertices, box (%.1f %.1f %.1f)-(%.1f %.1f %.1f)",
			s_Models.GetCount(), totalBatches, totalVerts, s_Min.x, s_Min.y, s_Min.z, s_Max.x, s_Max.y, s_Max.z);
	}
	if (!s_Models.GetCount()) return 1;
	if (ARGS.Get("nogfx")) return 0;

	// window + device
	int w = 1280, h = 720;
	ARGS.Get("width", w, w);
	ARGS.Get("height", h, h);
	PIPE.SetRes(w, h, 32);
	PIPE.InitClass();

	Vector3 center = (s_Min + s_Max) * 0.5f;
	float radius = (s_Max - s_Min).Mag() * 0.5f;
	if (radius < 1.0f) radius = 1.0f;
	float dist = s_IsCarAssemble ? (radius * 1.55f) : (radius * 2.2f);
	const char *ds = 0;
	if (ARGS.Get("dist", 0, &ds) && ds) dist = (float)atof(ds);

	float camTheta = 0.6f;
	float camPhi = 0.35f;
	const char *ys = 0, *ps = 0;
	if (ARGS.Get("yaw", 0, &ys) && ys) camTheta = (float)atof(ys) * 3.14159265f / 180.0f;
	if (ARGS.Get("pitch", 0, &ps) && ps) camPhi = (float)atof(ps) * 3.14159265f / 180.0f;
	bool orbit = !ARGS.Get("noorbit") && !ys;

	devPolarCam cam;
	cam.Init(dist, camTheta, camPhi);
	cam.SetOffset(center);
	ageClearColor = 0x203040;
	const char *bg = 0;
	if (ARGS.Get("bg", 0, &bg) && bg) ageClearColor = (u32)strtoul(bg, 0, 16);   // -bg ff00ff: expose holes

	RSTATE.SetLighting(false);          // shading is baked into the vertex colours (Shade)
	s_Lit = !ARGS.Get("unlit");
	s_Center = center;
	float az = camTheta;
	do {
		if (orbit) az += 0.004f;
		cam.SetAzimuth(az);
		cam.Update();
		// key light: above and a little behind-left of the camera so highlights follow the orbit
		s_CamPos = cam.GetWorldMtx().d;
		{
			Vector3 toCam = s_CamPos - center; toCam.y = 0.0f;
			if (toCam.MagSq() < 1e-6f) toCam.Set(0.0f, 0.0f, 1.0f); else toCam.Normalize();
			Vector3 up(0.0f, 1.0f, 0.0f), right; right.Cross(up, toCam);
			s_LightDir = toCam * 0.55f + up * 1.0f + right * 0.45f;
			s_LightDir.Normalize();
		}
		ageBeginFrame();
		RSTATE.SetCamera(cam.GetWorldMtx());
		if (PIPE.GetViewport()) PIPE.GetViewport()->SetPerspective(60.0f * 3.14159265f / 180.0f, 0.5f, dist * 4.0f + radius * 4.0f);
		RSTATE.SetTexture(0);
		DrawModels();
		ageEndFrame();
	} while (!ageExit());

	return 0;
}

int main(int argc, char **argv)
{
	ARGS.Init(argc, argv);
	extern int ExceptMain();
	return ExceptMain();
}
