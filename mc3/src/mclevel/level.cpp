#include	"data/string.h"
#include	"data/args.h"
#include	"data/assetcfg.h"
#include	"data/assetcache.h"
#include	"data/memory.h"
#include	"data/memstats.h"
#include	"data/callback.h"
#include	"data/chunk.h"
#include	"data/param.h"
#include	"core/stream.h"
#include	"core/output.h"
#include	"gfx/rstate.h"
#include	"gfx/simple.h"
#include	"gfx/loadimg.h"
#include	"effects/track.h"
#include	"bank/bank.h"
#include	"bank/bkmgr.h"
#include	"data/resource.h"

#include	"rmcore/light.h"
#include	"rmcore/state.h"
#include	"rmcore/model.h"
#include	"rmcore/cpvpalette.h"
#include	"rmcore/rscgeom.h"
#include	"gfx/model.h"
#include	"gfx/texture.h"
#include	<map>

#include	"profile/profiler.h"
#include	"data/base.h"
#include	"mclevel/level.h"
#include	"mclevel/citymodel.h"
#include "core/output.h"
#include "vector/matrix34.h"
#include "vector/matrix44.h"
#include "vector/vector4.h"
#include "parse/fileio.h"
#include "ptx_base/draw.h"
#include "mccullable/cullableclass.h"
#include "gfx/ptsprite.h"
class Matrix34;
#include	"mclevel/instcitymodel.h"
#include	"mclevel/hood.h"
#include	"mclevel/pvs.h"
#include	"core/types.h"
#include	"mcoccluder/occluder.h"
#include	"rmcore/shader.h"
#include	"rmcore/shaderbasic.h"
#include	"rmcore/shadercomplex.h"
#include	"rmcore/shadertemplate.h"
#include	"rmcore/texturedefault.h"
#include	"mcdata/config.h"
#include	"snd_control/control.h"
#include	"atl/array.h"
#include	"mccullable/cullablemgr.h"
#include	"mcgfx/mcgfx.h"
#include	"parse/fileio.h"
#include	"gfx/misc.h"

#include	"gfx/vglext.h"

#include	"mceffects/envmap.h"

mcCity		*MCCITY = NULL;

#if __XBOX | __XENON
#else
#endif

float	mcCity::s_fCellSize = 40.0f;

static bool s_MapSnapShotActive = false;

rmcShaderGroup	*mcCity::s_pShaderGroups;

static std::map<u32, gfxModel *> s_cityGfxModelCache;

mcCity::mcCity	(void)
{
	MCCITY = this;

	m_bResourced = false;

	m_nNumShaderGroups = 0;

	m_pShaderGroups = NULL;
	s_pShaderGroups = NULL;

	m_pSkyShaderGroup = NULL;

	m_pPropShaderGroup = NULL;

	m_nNumHoods = 0;
	m_pHoods = NULL;

	m_pPVS = NULL;
	m_bUsePVS = true;
	m_fNoPVSDrawDist = 150.0f;

	m_vExtentsMin.Zero();
	m_vExtentsMax.Zero();

	m_nNumCells = 0;
	m_nNumCellsX = 0;
	m_nNumCellsZ = 0;

	m_pCPVPalette = NULL;
	m_pCPVPaletteCopy = NULL;

	m_pCamera = NULL;

	m_nActiveViewport = 0;

	m_nCellMinX = 0;
	m_nCellMinZ = 0;

	m_vAmbientLightingColor.Zero();
	m_vDirectionalLightingColor.Zero();
	m_vDirectionalLightingDirection.Zero();


	m_pEnvMap = NULL;
}

mcCity::~mcCity	(void)
{
	delete []	m_pShaderGroups;

	s_pShaderGroups = NULL;


	delete	m_pEnvMap;

	delete	m_pPVS;

	delete []	m_pHoods;

	delete m_pCPVPaletteCopy;
	delete m_pCPVPalette;

	mcInstCityModelClass::Release();
	mcCityModelClass::Release();

	mcSkyHatClass::Release();

	delete m_pSkyShaderGroup;
	m_pSkyShaderGroup = NULL;
	delete m_pPropShaderGroup;
	m_pPropShaderGroup = NULL;

	for (auto &pair : s_cityGfxModelCache)
	{
		delete pair.second;
	}
	s_cityGfxModelCache.clear();

	MCCITY = NULL;
}



void	mcCity::CalcRelCellXZ	(const Vector3 &vPos, int &nX, int &nZ) const
{
	CalcAbsCellXZ(vPos, nX, nZ);

	nX -= m_nCellMinX;
	nZ -= m_nCellMinZ;
}

void	mcCity::CalcAbsCellXZ	(const Vector3 &vPos, int &nX, int &nZ)
{
	nX = int(floor(vPos.x / GetCellSize()));
	nZ = int(floor(vPos.z / GetCellSize()));
}

void mcCity::SetupFog(bool windows)
{
	float t = 0.0f;
	if ( s_MapSnapShotActive ) return;

	Vector3 color;
	GetCurrentCondition().GetFogColor(t, color);

	Vector3 c(1.0f, 1.0f, 1.0f);
	float u = m_pSkyHat->GetLightningColor(c);
	c.Scale(Vector3(1.0f, 1.0f, 1.0f), u * 0.1f);
	color += c;
	if(color.x < 0.0f) color.x = 0.0f;
	if(color.y < 0.0f) color.y = 0.0f;
	if(color.z < 0.0f) color.z = 0.0f;

	if(color.x > 1.0f) color.x = 1.0f;
	if(color.y > 1.0f) color.y = 1.0f;
	if(color.z > 1.0f) color.z = 1.0f;

	float fogStart = (1.0f - u) * GetCurrentCondition().GetFogStart(t);

	if(windows)
	{
		rmcState::SetFogParams(mkfrgb(color), fogStart, GetCurrentCondition().GetFogEnd(t) * GetCurrentCondition().mLightFogScale, 0.0f, 1.0f);
	}
	else
	{
		{
			// PC port: fog census (-texlog): the first values the city pushes
			static int sFogLogged = 0;
			if (sFogLogged < 3 && ARGS.Get("texlog"))
			{
				sFogLogged++;
				Displayf("mcCity::SetupFog: t=%.2f color (%.2f %.2f %.2f) lightning %.2f start %.1f end %.1f clamp %.2f (indoor start %.1f end %.1f)", t, color.x, color.y, color.z, u,
				         GetCurrentCondition().GetFogStart(t), GetCurrentCondition().GetFogEnd(t), GetCurrentCondition().GetFogClamp(t), GetCurrentCondition().GetFogStart(1.0f), GetCurrentCondition().GetFogEnd(1.0f));
			}
		}
		rmcState::SetFogParams(mkfrgb(color), GetCurrentCondition().GetFogStart(t), GetCurrentCondition().GetFogEnd(t),  0.0f, GetCurrentCondition().GetFogClamp(t));
	}

}

void mcCity::SetGlobalRmLighting(rmcLightGroup *rmLights, int gfxIdx, float mod)
{
	rmLights->Ambient.x = m_CurrentCondition.mAmbientLight.x;
	rmLights->Ambient.y = m_CurrentCondition.mAmbientLight.y;
	rmLights->Ambient.z = m_CurrentCondition.mAmbientLight.z;
	
	if (rmLights->Ambient.x <= 0.001f && rmLights->Ambient.y <= 0.001f && rmLights->Ambient.z <= 0.001f)
	{
		rmLights->Ambient.x = m_vAmbientLightingColor.x;
		rmLights->Ambient.y = m_vAmbientLightingColor.y;
		rmLights->Ambient.z = m_vAmbientLightingColor.z;
	}

	const float minAmb = 0.22f;
	if (rmLights->Ambient.x < minAmb) rmLights->Ambient.x = minAmb;
	if (rmLights->Ambient.y < minAmb) rmLights->Ambient.y = minAmb;
	if (rmLights->Ambient.z < minAmb) rmLights->Ambient.z = minAmb;

	Vector3 dir = m_CurrentCondition.GetGlobalLightingDir();
	Vector3 dirCol(m_CurrentCondition.mDirectionalLightColor.x,
	               m_CurrentCondition.mDirectionalLightColor.y,
	               m_CurrentCondition.mDirectionalLightColor.z);

	if (dirCol.Mag2() <= 0.001f)
	{
		dirCol = m_vDirectionalLightingColor;
		if (m_vDirectionalLightingDirection.Mag2() > 0.001f)
			dir = m_vDirectionalLightingDirection;
	}

	rmLights->Dir[gfxIdx] = dir;
	rmLights->Color[gfxIdx] = dirCol;
	rmLights->Color[gfxIdx].Scale(mod);

	rmLights->Color[gfxIdx].Clamp(0.0f, 1.0f);

	rmLights->Intensity[gfxIdx] = 1.0f;
	rmLights->Mode[gfxIdx] = 1;			// PC port: directional
}


#include	"data/resourcehelpers.h"

IMPLEMENT_PLACE(mcCity);

// PC port: datResourceBuilder for mcCity
#include "data/rscimage.h"

#include "mcgfx/mcgfx.h"
#include "gfx/texture.h"

// PC port: the city's shader group 0 (see rmcore/rscgeom.h "City textures") and the
// textures built from the page file, one per shader index, registered as "city_tex_<i>".
static atArray<rscTexInfo> s_cityShaders;
static atArray<gfxTexture *> s_cityTextures;
static atArray<u8> s_cityTexTried;
static atArray<u8> s_cityWinTried;   // city_window layer textures: 0 untried, 1 failed, 2 registered
static int s_cityTexLoaded = 0;

static void s_ResetCityTextures(const datResourceImage &image)
{
	s_cityShaders.Reset();
	s_cityTextures.Reset();
	s_cityTexTried.Reset();
	s_cityWinTried.Reset();
	s_cityTexLoaded = 0;
	if (rscDecodeCityShaders(image, -1, s_cityShaders) > 0)
	{
		s_cityTextures.Resize(s_cityShaders.GetCount());
		s_cityTexTried.Resize(s_cityShaders.GetCount());
		for (int i = 0; i < s_cityShaders.GetCount(); i++) { s_cityTextures[i] = NULL; s_cityTexTried[i] = 0; }
	}
}

static gfxTexture *s_CityTexture(const datResourceImage &image, int shaderIdx)
{
	if (shaderIdx < 0 || shaderIdx >= s_cityShaders.GetCount()) return NULL;
	if (!s_cityTexTried[shaderIdx])
	{
		s_cityTexTried[shaderIdx] = 1;
		char name[64];
		formatf(name, sizeof(name), "city_tex_%d", shaderIdx);
		if (s_cityShaders[shaderIdx].isWater)
		{
			// Water: modulated by basecolor alpha in PS2 (0.75 opacity over underwater bed)
			s_cityTextures[shaderIdx] = rscLoadCityTexture(image, CITY_PAGE_FILE_SLOT, s_cityShaders[shaderIdx], name, NULL, 96);
			if (s_cityTextures[shaderIdx])
			{
				s_cityTextures[shaderIdx]->SetHasAlpha(true);
				s_cityTextures[shaderIdx]->SetAllTranslucent(true);
			}
		}
		else
		{
			s_cityTextures[shaderIdx] = rscLoadCityTexture(image, CITY_PAGE_FILE_SLOT, s_cityShaders[shaderIdx], name);
		}
		if (s_cityTextures[shaderIdx]) s_cityTexLoaded++;
	}
	return s_cityTextures[shaderIdx];
}

// PC port: a city_window shader's window layer, registered as "city_win_<i>" with the
// pass's GS colour modulation baked in (RGB * WindowTint / 128, basecolor %2 %3 %4), so
// gfxModel's stage-2 combine 2 - base + layer * base texture alpha - reproduces the PS2's
// second pass ("Cs 0 Ad Cd" over the base's written alpha, the window mask).
static bool s_CityWindowTexture(const datResourceImage &image, int shaderIdx)
{
	if (shaderIdx < 0 || shaderIdx >= s_cityShaders.GetCount()) return false;
	if (s_cityWinTried.GetCount() != s_cityShaders.GetCount())
	{
		s_cityWinTried.Resize(s_cityShaders.GetCount());
		for (int i = 0; i < s_cityWinTried.GetCount(); i++) s_cityWinTried[i] = 0;
	}
	if (!s_cityWinTried[shaderIdx])
	{
		s_cityWinTried[shaderIdx] = 1;
		const rscTexInfo &sh = s_cityShaders[shaderIdx];
		rscTexInfo layer;
		char name[64];
		formatf(name, sizeof(name), "city_win_%d", shaderIdx);
		if (sh.layerTex && rscParseTexInfo(image, sh.layerTex, layer) &&
		    rscLoadCityTexture(image, CITY_PAGE_FILE_SLOT, layer, name, sh.tint, 128))
			s_cityWinTried[shaderIdx] = 2;
	}
	return s_cityWinTried[shaderIdx] == 2;
}

// PC port: texture census per city part (set by the type loops before each model build)
static int s_censusPart = -1;
static int s_censusGeoms[5], s_censusTextured[5], s_censusNoShader[5], s_censusPsm4[5], s_censusFailed[5];
static int s_censusVerts[5], s_censusCpvVerts[5];   // vertices decoded / with a CPV colour, per part
static int s_censusLogged[5];

static rmcModel *s_GetOrCreateCityRmcModel(const datResourceImage &image, u32 modelAddr, int part)
{
	if (!modelAddr || !image.IsValidAddress(modelAddr, 0x20)) return NULL;

	auto it = s_cityGfxModelCache.find(modelAddr);
	gfxModel *lodGfx = (it != s_cityGfxModelCache.end()) ? it->second : NULL;
	if (!lodGfx)
	{
		rscModel decoded;
		if (!rscDecodeModel(image, modelAddr, decoded))
			return NULL;

		lodGfx = new gfxModel();
		atArray<int> shaderToMaterial;
		if (s_cityShaders.GetCount())
		{
			shaderToMaterial.Resize(s_cityShaders.GetCount());
			for (int i = 0; i < shaderToMaterial.GetCount(); i++) shaderToMaterial[i] = -1;
			for (int g = 0; g < decoded.geometries.GetCount(); g++)
			{
				int idx = decoded.geometries[g].shaderIdx;
				if (s_censusPart >= 0 && s_censusPart < 5)
				{
					int p = s_censusPart;
					s_censusGeoms[p]++;
					for (int b = 0; b < decoded.geometries[g].batches.GetCount(); b++)
					{
						const rscGeomBatch &bt = decoded.geometries[g].batches[b];
						s_censusVerts[p] += bt.verts.GetCount();
						if (bt.colors.GetCount() == bt.verts.GetCount()) s_censusCpvVerts[p] += bt.verts.GetCount();
					}
					bool inRange = idx >= 0 && idx < s_cityShaders.GetCount();
					gfxTexture *t = inRange ? s_CityTexture(image, idx) : NULL;
					if (!inRange) s_censusNoShader[p]++;
					else if (t) s_censusTextured[p]++;
					else if (s_cityShaders[idx].psm == 0x14) s_censusPsm4[p]++;
					else s_censusFailed[p]++;
					if (p == 1 && s_censusLogged[p]++ < 12)
						Displayf("mcCity ground geometry: model %08x geom %d shader %d %s psm %#x %ux%u mips %d entry %u", modelAddr, g, idx,
						         !inRange ? "OUT OF RANGE" : t ? "textured" : "NO TEXTURE", inRange ? s_cityShaders[idx].psm : 0,
						         inRange ? s_cityShaders[idx].width : 0, inRange ? s_cityShaders[idx].height : 0, inRange ? s_cityShaders[idx].numMips : 0,
						         inRange ? s_cityShaders[idx].mipEntry[0] : 0);
				}
				if (idx < 0 || idx >= shaderToMaterial.GetCount() || shaderToMaterial[idx] >= 0) continue;
				gfxModelMaterial mat;
				mat.name = "city_shader";
				mat.diffuse[0] = mat.diffuse[1] = mat.diffuse[2] = 1.0f;
				mat.packet_count = 0;
				mat.primitive_count = 0;
				if (s_CityTexture(image, idx))
				{
					char name[64];
					formatf(name, sizeof(name), "city_tex_%d", idx);
					mat.texture_name = name;
				}
				// PC port: the PS2 draw state comes from the model's city pass and the shader's
				// class / template (citymodel.cpp pass states + .shadert overrides), not from
				// whether the texture has alpha.
				const rscTexInfo &sh = s_cityShaders[idx];
				mat.city_blend = rscCityBlendState(part, sh.shaderType, sh.templateSlot, sh.isWater);
				if (ARGS.Get("cityparttint"))
				{
					// -cityparttint: colour-code city materials by their model's draw part
					// (MAIN red, GROUND green, REFLECT blue, HDR yellow, ALPHA magenta)
					static const float kTint[5][3] = { {1.6f, 0.4f, 0.4f}, {0.4f, 1.6f, 0.4f}, {0.4f, 0.4f, 1.6f}, {1.6f, 1.6f, 0.3f}, {1.6f, 0.3f, 1.6f} };
					if (part >= 0 && part < 5)
						for (int c = 0; c < 3; c++) mat.diffuse[c] = kTint[part][c];
				}
				// mcShaderCityWindow (city_window): the window layer pass (texture %1, texsrc 1,
				// basecolor = WindowTint) lands on a base that wrote its alpha - the window mask -
				// and blends "Cs 0 Ad Cd": base + layer * base alpha, stage-2 combine 2.
				if (sh.shaderType == 16 && sh.templateSlot == 1)
				{
					bool isDaytime = mcConfig::IsDaytime();
					if (isDaytime)
					{
						// PC port: the PS2 swapped this layer for "__envmap__", the live city
						// environment map (mcShaderCityWindow::Load); the port had to settle
						// for a canned image while every mcCityEnvMap body was empty.
						mat.texture_name2 = mcCityEnvMap::LiveEnvMapEnabled()
							? mcCityEnvMap::GetLiveTextureName()
							: "fx_car_viewer_envmap";
						mat.tex2_mode = 2;
						mat.tex2_reflect = true;
					}
					else if (s_CityWindowTexture(image, idx))
					{
						char name[64];
						formatf(name, sizeof(name), "city_win_%d", idx);
						mat.texture_name2 = name;
						mat.tex2_mode = 2;
					}
				}
				// city_road (road instance, class 17): texture %1 is a shared grime / detail layer
				// at scales/scalet (4x the base UVs) blended "normal" over the base, keeping the
				// vertex (CPV) colour: stage-2 combine 3.
				else if (sh.shaderType == 17 && sh.layerTex && !ARGS.Get("nocitydetail"))
				{
					// The 302 road shaders reach their detail texture through their own wrapper
					// objects but share a few resident blocks: key it by the block so each one is
					// decoded and uploaded once.  -citydetailscale <f> scales its alpha, i.e. the
					// pass's blend weight (1.0 = the pack's; 0 = off).
					float detailScale = 1.0f;
					ARGS.Get("citydetailscale", 1.0f, detailScale);
					rscTexInfo detail;
					char name[64];
					bool have = detailScale > 0.0f && rscParseTexInfo(image, sh.layerTex, detail);
					if (have)
					{
						formatf(name, sizeof(name), "city_detail_%08x", detail.residentAddr ? detail.residentAddr : sh.layerTex);
						if (!gfxTextureResident(name))
							have = rscLoadCityTexture(image, CITY_PAGE_FILE_SLOT, detail, name, NULL, (int)(128.0f * detailScale + 0.5f)) != NULL;
					}
					if (have)
					{
						mat.texture_name2 = name;
						mat.tex2_mode = 3;
						mat.tex2_uvscale[0] = sh.layerScale[0];
						mat.tex2_uvscale[1] = sh.layerScale[1];
					}
				}
				// water.shadert: UV-scrolled animated water surface
				else if (sh.isWater)
				{
					mat.scroll_u = sh.scrollU;
					mat.scroll_v = sh.scrollV;
					mat.no_zwrite = true;
					// Natural water tint
					mat.diffuse[0] = 0.85f;
					mat.diffuse[1] = 0.95f;
					mat.diffuse[2] = 1.0f;
				}
				shaderToMaterial[idx] = lodGfx->materials.GetCount();
				lodGfx->materials.Append(mat);
			}
		}
		if (!gfxModelFromRscModel(decoded, 0, *lodGfx, shaderToMaterial.GetCount() ? &shaderToMaterial[0] : NULL, shaderToMaterial.GetCount()) || lodGfx->packets.GetCount() == 0)
		{
			delete lodGfx;
			return NULL;
		}

		lodGfx->BuildDrawOrder();
		s_cityGfxModelCache[modelAddr] = lodGfx;
	}

	rmcModel *rmcMdl = new rmcModel();
	rmcMdl->SetModel(0, lodGfx);
	Vector3 bmin, bmax;
	lodGfx->GetBoundingBox(bmin, bmax);
	rmcMdl->SetBoundingBox(bmin, bmax);
	return rmcMdl;
}

// PC port: per-part model census of the city types (main, ground, reflect, HDR, alpha)
static int s_cityPartModels[partNUMPARTS], s_cityPartBuilt[partNUMPARTS], s_cityPartFailedLogged[partNUMPARTS];

static mcCityModelType *s_GetOrCreateCityModelType(const datResourceImage &image, u32 typeAddr, mcCityModelClass *cityModelClass, std::map<u32, mcCityModelType *> &typeCache)
{
	if (!typeAddr || !image.IsValidAddress(typeAddr, 0x64)) return NULL;

	auto it = typeCache.find(typeAddr);
	if (it != typeCache.end())
	{
		it->second->AddRef();
		return it->second;
	}

	u32 namePtr = image.ReadU32(typeAddr + 0x08);
	const char *typeName = namePtr ? image.ReadString(namePtr) : "unknown_city_type";

	mcCityModelType *type = new mcCityModelType();
	type->SetName(typeName);

	Vector3 center(image.ReadFloat(typeAddr + 0x2c), image.ReadFloat(typeAddr + 0x30), image.ReadFloat(typeAddr + 0x34));
	float radius = image.ReadFloat(typeAddr + 0x38);
	type->GetModelInfo().SetCenter(center);
	type->GetModelInfo().SetRadius(radius);

	for (unsigned int lod = 0; lod < NUM_CITY_LODS; lod++)
	{
		for (unsigned int part = 0; part < partNUMPARTS; part++)
		{
			u32 modelAddr = image.ReadU32(typeAddr + 0x3c + (lod * 5 + part) * 4);
			if (modelAddr)
			{
				s_cityPartModels[part]++;
				s_censusPart = (int)part;
				rmcModel *m = s_GetOrCreateCityRmcModel(image, modelAddr, (int)part);
				s_censusPart = -1;
				if (m)
				{
					s_cityPartBuilt[part]++;
					type->GetModelInfo().SetModel(lod, (mcBuildingParts)part, m);
				}
				else if (s_cityPartFailedLogged[part]++ < 3)
					Displayf("mcCity: type '%s' lod %u part %u: model at %08x (vtable %08x) did not decode", typeName, lod, part, modelAddr,
					         image.IsValidAddress(modelAddr, 4) ? image.ReadU32(modelAddr) : 0);
			}
		}
	}

	cityModelClass->AddType(type);
	typeCache[typeAddr] = type;
	return type;
}

static mcInstCityModelType *s_GetOrCreateInstCityModelType(const datResourceImage &image, u32 typeAddr, mcInstCityModelClass *instCityModelClass, std::map<u32, mcInstCityModelType *> &typeCache)
{
	if (!typeAddr || !image.IsValidAddress(typeAddr, 0x64)) return NULL;

	auto it = typeCache.find(typeAddr);
	if (it != typeCache.end())
	{
		it->second->AddRef();
		return it->second;
	}

	u32 namePtr = image.ReadU32(typeAddr + 0x08);
	const char *typeName = namePtr ? image.ReadString(namePtr) : "unknown_inst_type";

	mcInstCityModelType *type = new mcInstCityModelType();
	type->SetName(typeName);

	Vector3 center(image.ReadFloat(typeAddr + 0x2c), image.ReadFloat(typeAddr + 0x30), image.ReadFloat(typeAddr + 0x34));
	float radius = image.ReadFloat(typeAddr + 0x38);
	type->GetModelInfo().SetCenter(center);
	type->GetModelInfo().SetRadius(radius);

	for (unsigned int lod = 0; lod < NUM_CITY_LODS; lod++)
	{
		for (unsigned int part = 0; part < partNUMPARTS; part++)
		{
			u32 modelAddr = image.ReadU32(typeAddr + 0x3c + (lod * 5 + part) * 4);
			if (modelAddr)
			{
				rmcModel *m = s_GetOrCreateCityRmcModel(image, modelAddr, (int)part);
				if (m)
				{
					type->GetModelInfo().SetModel(lod, (mcBuildingParts)part, m);
				}
			}
		}
	}

	instCityModelClass->AddType(type);
	typeCache[typeAddr] = type;
	return type;
}

static void s_DecodeCityShaderGroups(const datResourceImage &image, u32 pShaderGroups, int numShaderGroups)
{
	if (!pShaderGroups || numShaderGroups <= 0) return;
	Displayf("s_DecodeCityShaderGroups: decoding textures for %d shader groups at 0x%08x...", numShaderGroups, pShaderGroups);

	int totalDecoded = 0;
	for (int g = 0; g < numShaderGroups; g++)
	{
		u32 gAddr = pShaderGroups + (u32)g * 16;
		if (!image.IsValidAddress(gAddr, 16)) continue;

		u32 tablePtr = image.ReadU32(gAddr + 4);
		u32 count = image.ReadU32(gAddr + 8) & 0xffff;
		if (!tablePtr || count == 0 || !image.IsValidAddress(tablePtr, count * 4)) continue;

		for (u32 i = 0; i < count; i++)
		{
			u32 shPtr = image.ReadU32(tablePtr + i * 4);
			if (!shPtr || !image.IsValidAddress(shPtr, 12)) continue;

			u32 imgPtr = image.ReadU32(shPtr + 8);
			if (!imgPtr || !image.IsValidAddress(imgPtr, 16)) continue;

			u32 vt = image.ReadU32(imgPtr);
			u32 imgAddr = 0;
			if (vt == 0x007a2320) {
				imgAddr = imgPtr;
			} else if (vt == 0x007a20e0) {
				if (image.IsValidAddress(imgPtr + 0x10, 16) && image.ReadU32(imgPtr + 0x10) == 0x007a2320) {
					imgAddr = imgPtr + 0x10;
				} else {
					u32 subPtr = image.ReadU32(imgPtr + 0x0c);
					if (subPtr && image.IsValidAddress(subPtr, 16) && image.ReadU32(subPtr) == 0x007a2320) {
						imgAddr = subPtr;
					}
				}
			}

			if (imgAddr) {
				char imgTexName[64];
				if (rscDecodeCityImage(image, imgAddr, imgTexName, sizeof(imgTexName))) {
					char shTexName[64];
					snprintf(shTexName, sizeof(shTexName), "city_sh_%d_%d", g, i);
					gfxTexture *tex = gfxGetTexture(imgTexName);
					if (tex) {
						gfxAssociateTexture(tex, shTexName);
						char defaultShName[64];
						snprintf(defaultShName, sizeof(defaultShName), "city_sh_0_%d", i);
						if (!gfxTextureResident(defaultShName)) {
							gfxAssociateTexture(tex, defaultShName);
						}
						char canonName[64];
						snprintf(canonName, sizeof(canonName), "city_sh_%d", i);
						if (!gfxTextureResident(canonName)) {
							gfxAssociateTexture(tex, canonName);
						}
					}
					totalDecoded++;
				}
			}
		}
	}
	Displayf("s_DecodeCityShaderGroups: %d shader textures decoded and registered", totalDecoded);
}

template <>
mcCity *datResourceBuilder<mcCity>::Build(const datResourceImage &image)
{
	u32 cityAddr = image.GetBase();
	if (!cityAddr || !image.IsValidAddress(cityAddr, 0x80))
	{
		Warningf("mcCity::Build: invalid root address 0x%08x in '%s'", cityAddr, image.GetName());
		return NULL;
	}

	datResourceTokenizer root(image, cityAddr);
	u32 vptr = root.GetVTable();
	bool bResourced = root.GetBool();
	root.Align(4);
	int numShaderGroups = root.GetInt();
	u32 pShaderGroups = root.GetPtr();
	u32 pSkyShaderGroup = root.GetPtr();
	int numHoods = root.GetInt();
	u32 pHoods = root.GetPtr();
	u32 pPVS = root.GetPtr();
	bool bUsePVS = root.GetBool();
	root.Align(4);
	float noPvsDrawDist = root.GetFloat();

	s_ResetCityTextures(image);
	mcCity *city = new mcCity;
	city->m_bResourced = true;
	city->m_nNumShaderGroups = numShaderGroups;
	city->m_bUsePVS = bUsePVS;
	city->m_fNoPVSDrawDist = (noPvsDrawDist < 1100.0f) ? 1100.0f : noPvsDrawDist;
	{
		float over = 0.0f;
		if (ARGS.Get("drawdist", 0, over) && over > 1.0f)
			city->m_fNoPVSDrawDist = over;
	}
	Displayf("mcCity: PVS %08x (pack says use %d, no-PVS draw distance %.0f -> %.0f)", pPVS, (int)bUsePVS, noPvsDrawDist, city->m_fNoPVSDrawDist);

	// PC port: Load CPV palette from city resource pack
	if (image.IsValidAddress(cityAddr + 0x2c, 8))
	{
		u32 pPalette = image.ReadU32(cityAddr + 0x2c);
		u32 pPaletteCopy = image.ReadU32(cityAddr + 0x30);
		if (pPalette && image.IsValidAddress(pPalette, 256 * 16))
		{
			city->m_pCPVPalette = new rmcCpvPalette;
			city->m_pCPVPalette->InitFromImage(image, pPalette);
			if (pPaletteCopy && image.IsValidAddress(pPaletteCopy, 256 * 16))
			{
				city->m_pCPVPaletteCopy = new rmcCpvPalette;
				city->m_pCPVPaletteCopy->InitFromImage(image, pPaletteCopy);
			}
			rmcCpvPalette::SetCurrent(*city->m_pCPVPalette);
			Displayf("mcCity: CPV palette loaded (256 colors) from 0x%08x", pPalette);
		}
	}

	// PC port: Read level extents and cell grid dimensions from city resource pack.
	// Extents are at root + 0x238 in the city pack.
	if (image.IsValidAddress(cityAddr + 0x238, 0x48))
	{
		datResourceTokenizer ext(image, cityAddr + 0x238);
		ext.GetVector3(city->m_vExtentsMin);
		ext.GetVector3(city->m_vExtentsMax);
		ext.Skip(24);
		city->m_nNumCells = ext.GetInt();
		city->m_nNumCellsX = ext.GetInt();
		city->m_nNumCellsZ = ext.GetInt();
		city->m_nCellMinX = ext.GetInt();
		city->m_nCellMinZ = ext.GetInt();
		Displayf("mcCity: extents min (%.1f, %.1f, %.1f) max (%.1f, %.1f, %.1f), cells %d (%dx%d, min %d, %d)",
		         city->m_vExtentsMin.x, city->m_vExtentsMin.y, city->m_vExtentsMin.z,
		         city->m_vExtentsMax.x, city->m_vExtentsMax.y, city->m_vExtentsMax.z,
		         city->m_nNumCells, city->m_nNumCellsX, city->m_nNumCellsZ,
		         city->m_nCellMinX, city->m_nCellMinZ);
	}

	if (numShaderGroups > 0)
	{
		city->m_pShaderGroups = new rmcShaderGroup[numShaderGroups];
		mcCity::s_pShaderGroups = city->m_pShaderGroups;
		s_DecodeCityShaderGroups(image, pShaderGroups, numShaderGroups);
	}

	// PC port: Allocate sky shader group and decode resident sky textures
	if (!city->m_pSkyShaderGroup)
		city->m_pSkyShaderGroup = new rmcShaderGroup;

	if (pSkyShaderGroup && image.IsValidAddress(pSkyShaderGroup, 16))
	{
		u32 tablePtr = image.ReadU32(pSkyShaderGroup + 4);
		u32 count = image.ReadU32(pSkyShaderGroup + 8) & 0xffff;
		if (tablePtr && count > 0 && image.IsValidAddress(tablePtr, count * 4))
		{
			int skyDecoded = 0;
			for (u32 i = 0; i < count; i++)
			{
				u32 shPtr = image.ReadU32(tablePtr + i * 4);
				if (!shPtr || !image.IsValidAddress(shPtr, 12)) continue;
				u32 imgPtr = image.ReadU32(shPtr + 8);
				rscTexInfo texInfo;
				if (rscParseTexInfo(image, imgPtr, texInfo))
				{
					char texName[64];
					snprintf(texName, sizeof(texName), "sky_sh_%u", i);
					if (rscLoadCityTexture(image, -1, texInfo, texName))
						skyDecoded++;
				}
			}
			Displayf("mcCity: %d resident sky textures decoded from pSkyShaderGroup (0x%08x)", skyDecoded, pSkyShaderGroup);
		}
	}

	city->m_pCityModelClass = mcCityModelClass::CreateInstance();
	city->m_pInstCityModelClass = mcInstCityModelClass::CreateInstance();

	std::map<u32, mcCityModelType *> cityTypeCache;
	std::map<u32, mcInstCityModelType *> instTypeCache;
	std::unordered_map<u32, mcCityModel*> cityModelByAddr;
	std::unordered_map<u32, mcInstCityModel*> instModelByAddr;

	if (numHoods > 0 && pHoods && image.IsValidAddress(pHoods, (u32)numHoods * 20))
	{
		city->m_nNumHoods = numHoods;
		city->m_pHoods = new mcHood[numHoods];
		for (int h = 0; h < numHoods; h++)
		{
			datResourceTokenizer ht(image, pHoods + (u32)h * 20);
			const char *hoodName = ht.GetStringPtr();
			city->m_pHoods[h].Init(hoodName ? hoodName : "unknown");
			int numCityModels = ht.GetInt();
			u32 pCityModels = ht.GetPtr();
			int numInstCityModels = ht.GetInt();
			u32 pInstCityModels = ht.GetPtr();

			if (numCityModels > 0)
				city->m_pHoods[h].AllocCityModels(numCityModels);
			if (numInstCityModels > 0)
				city->m_pHoods[h].AllocInstCityModels(numInstCityModels);

			for (int m = 0; m < numCityModels; m++)
			{
				u32 mAddr = pCityModels + (u32)m * 28;
				if (!image.IsValidAddress(mAddr, 28)) continue;

				u32 typeAddr = image.ReadU32(mAddr + 0x0c);
				u8 minX = image.ReadU8(mAddr + 0x14);
				u8 maxX = image.ReadU8(mAddr + 0x15);
				u8 minZ = image.ReadU8(mAddr + 0x16);
				u8 maxZ = image.ReadU8(mAddr + 0x17);
				float radius = image.ReadFloat(mAddr + 0x18);

				mcCityModelType *type = s_GetOrCreateCityModelType(image, typeAddr, city->m_pCityModelClass, cityTypeCache);
				mcCityModel *cm = city->m_pHoods[h].GetCityModel(m);
				if (cm && type)
				{
					cm->SetType(type);
					cm->SetCellBounds(minX, maxX, minZ, maxZ);
					cm->SetRadius(radius);
					cm->SetGroup((int)image.ReadU8(mAddr + 0x0a));
					cm->Enable(true);
					cityModelByAddr[mAddr] = cm;
				}
			}

			for (int m = 0; m < numInstCityModels; m++)
			{
				u32 mAddr = pInstCityModels + (u32)m * 96;
				if (!image.IsValidAddress(mAddr, 96)) continue;

				u32 typeAddr = image.ReadU32(mAddr + 0x0c);
				u8 minX = image.ReadU8(mAddr + 0x14);
				u8 maxX = image.ReadU8(mAddr + 0x15);
				u8 minZ = image.ReadU8(mAddr + 0x16);
				u8 maxZ = image.ReadU8(mAddr + 0x17);
				float radius = image.ReadFloat(mAddr + 0x18);
				int group = (int)image.ReadU8(mAddr + 0x0a);

				Matrix34 mtx;
				mtx.a.x = image.ReadFloat(mAddr + 0x20);
				mtx.a.y = image.ReadFloat(mAddr + 0x24);
				mtx.a.z = image.ReadFloat(mAddr + 0x28);
				mtx.b.x = image.ReadFloat(mAddr + 0x2c);
				mtx.b.y = image.ReadFloat(mAddr + 0x30);
				mtx.b.z = image.ReadFloat(mAddr + 0x34);
				mtx.c.x = image.ReadFloat(mAddr + 0x38);
				mtx.c.y = image.ReadFloat(mAddr + 0x3c);
				mtx.c.z = image.ReadFloat(mAddr + 0x40);
				mtx.d.x = image.ReadFloat(mAddr + 0x44);
				mtx.d.y = image.ReadFloat(mAddr + 0x48);
				mtx.d.z = image.ReadFloat(mAddr + 0x4c);

				Vector3 cullCenter(image.ReadFloat(mAddr + 0x50), image.ReadFloat(mAddr + 0x54), image.ReadFloat(mAddr + 0x58));
				int cpvIndex = (int)image.ReadU32(mAddr + 0x1c);

				mcInstCityModelType *type = s_GetOrCreateInstCityModelType(image, typeAddr, city->m_pInstCityModelClass, instTypeCache);
				mcInstCityModel *im = city->m_pHoods[h].GetInstCityModel(m);
				if (im && type)
				{
					im->SetType(type);
					im->SetCellBounds(minX, maxX, minZ, maxZ);
					im->SetRadius(radius);
					im->SetGroup(group);
					im->SetRawMatrix(mtx);
					im->SetCullCenter(cullCenter);
					im->SetCPVIndex(cpvIndex);
					im->Enable(true);
					instModelByAddr[mAddr] = im;
				}
			}

			Displayf("  hood %d: '%s' (%d city models, %d inst models)",
			         h, hoodName ? hoodName : "unknown", numCityModels, numInstCityModels);
		}
	}

	// PC port: Initialize PVS from resource pack
	if (pPVS && bUsePVS && !ARGS.Get("nopvs"))   // -nopvs: distance culling only (as the loose-file path)
	{
		mcPVS *pvs = new mcPVS;
		if (pvs->InitFromResource(image, pPVS, cityModelByAddr, instModelByAddr))
		{
			city->m_pPVS = pvs;
			city->m_bUsePVS = true;
			Displayf("mcCity: PVS successfully initialized and enabled (%d city models, %d inst models)",
			         (int)cityModelByAddr.size(), (int)instModelByAddr.size());
		}
		else
		{
			delete pvs;
			city->m_pPVS = NULL;
			city->m_bUsePVS = false;
			Displayf("mcCity: PVS initialization failed, falling back to distance culling");
		}
	}
	else
	{
		city->m_pPVS = NULL;
		city->m_bUsePVS = false;
	}

	city->m_pSkyHat = new mcSkyHatClass;

	// PC port: Find and decode skyhat model from the city pack
	{
		const u8 *imgData = image.GetData();
		u32 imgSize = image.GetSize();
		u32 skyhatStrAddr = 0;
		for (u32 o = 0; o + 8 <= imgSize; o += 4)
		{
			if (memcmp(imgData + o, "skyhat_", 7) == 0)
			{
				skyhatStrAddr = image.ToAddress(o);
				break;
			}
		}
		u32 skyhatObjAddr = 0;
		if (skyhatStrAddr)
		{
			for (u32 o = 0; o + 4 <= imgSize; o += 4)
			{
				if (*(const u32 *)(imgData + o) == skyhatStrAddr)
				{
					u32 cand = image.ToAddress(o) - 8;
					if (image.IsValidAddress(cand, 0x20))
					{
						skyhatObjAddr = cand;
						break;
					}
				}
			}
		}

		int numSkyLayers = skyhatObjAddr ? (int)image.ReadU32(skyhatObjAddr + 0x10) : 1;
		if (numSkyLayers < 1 || numSkyLayers > 4) numSkyLayers = 1;

		for (int l = 0; l < numSkyLayers; l++)
		{
			u32 modelAddr = skyhatObjAddr ? image.ReadU32(skyhatObjAddr + 0x14 + (u32)l * 4) : 0x06931b80;
			if (!modelAddr || !image.IsValidAddress(modelAddr, 0x20))
			{
				if (l == 0) modelAddr = 0x06931b80;
				else continue;
			}

			rscModel decoded;
			if (rscDecodeModel(image, modelAddr, decoded) && decoded.geometries.GetCount() > 0)
			{
				gfxModel *skyGfx = new gfxModel();
				atArray<int> shaderToMaterial;
				for (int g = 0; g < decoded.geometries.GetCount(); g++)
				{
					int idx = decoded.geometries[g].shaderIdx;
					if (idx < 0) idx = 0;
					if (idx >= shaderToMaterial.GetCount())
					{
						int oldSz = shaderToMaterial.GetCount();
						shaderToMaterial.Resize(idx + 1);
						for (int k = oldSz; k <= idx; k++) shaderToMaterial[k] = -1;
					}
					if (shaderToMaterial[idx] < 0)
					{
						gfxModelMaterial mat;
						mat.name = "sky_shader";
						mat.diffuse[0] = mat.diffuse[1] = mat.diffuse[2] = 1.0f;
						mat.packet_count = 0;
						mat.primitive_count = 0;
						char name[64];
						snprintf(name, sizeof(name), "sky_sh_%d", idx);
						mat.texture_name = name;
						shaderToMaterial[idx] = skyGfx->materials.GetCount();
						skyGfx->materials.Append(mat);
					}
				}
				if (gfxModelFromRscModel(decoded, 0, *skyGfx, shaderToMaterial.GetCount() ? &shaderToMaterial[0] : NULL, shaderToMaterial.GetCount()) && skyGfx->packets.GetCount() > 0)
				{
					skyGfx->BuildDrawOrder();
					rmcModel *skyMdl = new rmcModel();
					skyMdl->SetModel(0, skyGfx);
					Vector3 bmin, bmax;
					skyGfx->GetBoundingBox(bmin, bmax);
					skyMdl->SetBoundingBox(bmin, bmax);
					city->m_pSkyHat->SetModel(l, skyMdl);
					Displayf("mcCity: attached skyhat layer %d (model 0x%08x, %d packets)", l, modelAddr, skyGfx->packets.GetCount());
				}
				else
				{
					delete skyGfx;
				}
			}
		}
	}

	MCCITY = city;
	for (int p = 0; p < 5; p++)
		Displayf("mcCity: part %d geometries %d: textured %d, shader out of range %d, PSMT4 %d, other %d; vertices %d, with CPV colour %d", p, s_censusGeoms[p], s_censusTextured[p], s_censusNoShader[p], s_censusPsm4[p], s_censusFailed[p],
		         s_censusVerts[p], s_censusCpvVerts[p]);
	Displayf("mcCity: city type models per part (main/ground/reflect/HDR/alpha): present %d/%d/%d/%d/%d, built %d/%d/%d/%d/%d",
	         s_cityPartModels[0], s_cityPartModels[1], s_cityPartModels[2], s_cityPartModels[3], s_cityPartModels[4],
	         s_cityPartBuilt[0], s_cityPartBuilt[1], s_cityPartBuilt[2], s_cityPartBuilt[3], s_cityPartBuilt[4]);
	Displayf("mcCity: successfully built from '%s' (%d shader groups, %d hoods, %d city types, %d inst types, %d gfx models)",
	         image.GetName(), numShaderGroups, numHoods, (int)cityTypeCache.size(), (int)instTypeCache.size(), (int)s_cityGfxModelCache.size());
	Displayf("mcCity: %d of %d shader-table textures built from the page file", s_cityTexLoaded, s_cityShaders.GetCount());
	return city;
}
