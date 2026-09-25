
#include	"rmcore/model.h"
#include	"rmcore/cpvpalette.h"

#include	"mclevel/citymodel.h"
#include	"mclevel/level.h"
#include	"mclevel/pvs.h"
#include	"profile/page.h"

#include	"mceffects/envmap.h"

#include	"mcdata/globaloptions.h"

#include	"mcgfx/mcgfx.h"

mcCityModelClass::mcCityModelClass	(void)
{

	mcCullableMgr::AddClass(m_eDrawOrder, this);
}

void	mcCityModelClass::SetRenderStates	(const mcPassTypes ePassMask) const
{
	rmcTextureFactory::PushInstance(*MCGFX->GetCityTextureFactory());

	rmcState::SetWorld(Matrix34::I);

	if (MCCITY && MCCITY->GetCPVPalette())
	{
		rmcSetCpvMode(true);
		rmcCpvPalette::SetCurrent(*MCCITY->GetCPVPalette());
		MCCITY->GetCPVPalette()->Download();
	}
	else
	{
		rmcSetCpvMode(false);
	}

	rmcState::SetForceColor(mkrgb(255, 255, 255));

	MCCITY->SetupFog(false);

	rmcState::SetFogBlend(true);

	rmcState::SetAlphaBlend(true);

	rmcLightGroup cityLightGroup;
	cityLightGroup.Reset();
	MCCITY->SetGlobalRmLighting(&cityLightGroup, 0);
	rmcState::SetLightingGroup(cityLightGroup);

	switch	(ePassMask)
	{
		case	dtCULLING:				break;

		case	dtREFLECTED_OBJECTS:	rmcState::SetLightingMode(rmclmNone);
										rmcState::SetBaseColor(mkrgb(255, 255, 255));
										rmcState::SetBlendSet(rmcbsNormal);
										rmcState::SetAlphaFunc(rmcafGreater);
										rmcState::SetAlphaRef(45);
										break;

		case	dtREFLECTING_GROUND:
										rmcState::SetLightingMode(rmclmDirectional);
										rmcState::SetBaseColor(mkrgb(255, 255, 255));
										rmcState::SetBlendSet(rmcBlendSetCustom_D3D(D3DBLEND_ONE, D3DBLEND_ZERO));
										rmcState::SetAlphaFunc(rmcafAlways);
										rmcState::SetAlphaRef(0);
										break;

		case	dtMAIN_SHADOWED:
										rmcState::SetLightingMode(rmclmDirectional);
										rmcState::SetBaseColor(mkrgba(255, 255, 255, 0));
										rmcState::SetBlendSet(rmcBlendSetCustom_D3D(D3DBLEND_ONE, D3DBLEND_ZERO));
										rmcState::SetAlphaFunc(rmcafAlways);
										rmcState::SetAlphaRef(0);
										rmcState::SetAlphaBlend(false);
										break;

		case	dtALPHA:
										rmcState::SetLightingMode(rmclmDirectional);
										rmcState::SetBaseColor(mkrgb(255, 255, 255));
										rmcState::SetBlendSet(rmcbsNormal);
										rmcState::SetAlphaFunc(rmcafGreater);
										rmcState::SetAlphaRef(45);
										break;

		default:						Assert(0 && "Rendering Pass Not Handled");
	}
}

void	mcCityModelClass::RestoreRenderStates	(const mcPassTypes  ) const
{

	// PC port: the opaque blend set of the ground / main passes must not leak
	// into whatever draws next (rmcState is shared, not per drawable)
	rmcState::SetBlendSet(rmcbsNormal);
	rmcState::SetLightingMode(rmclmNone);

	rmcTextureFactory::PopInstance();
}

// PC port: per-frame census of the no-PVS cull path (printed with the class census)
struct mcCityCullCensus { int active, rejDist, rejClip, rejOccl, rendered, noModel; bool logNames; void Reset() { active = rejDist = rejClip = rejOccl = rendered = noModel = 0; logNames = false; } };
static mcCityCullCensus g_cityCull;

void	mcCityModelClass::RenderAllTypes	(mcPassTypes ePassMask)
{
	mcPVS		*pPVS = MCCITY->GetPVS();
	const	int	nViewport = MCCITY->GetActiveViewport();
	int			nI;

	if	(MCCITY->UsePVS() && pPVS && nViewport >= 0 && nViewport < mc::MAX_VIEWPORTS)
	{
		const	int	nNumModels = pPVS->GetNumActiveCityModels(nViewport);

		for	(nI = 0; nI < nNumModels; nI++)
		{
			mcCityModel *pModel = pPVS->GetActiveCityModel(nViewport, nI);
			if (pModel)
				pModel->Render((mcPassTypes)ePassMask);
		}

		if ((ePassMask == dtMAIN_SHADOWED) && MCCITY->GetEnvMap() WIN32PC_ONLY(&& mcConfig::GetGlobalGameOptions().GetEnvironmentMappingOption()))
		{

			const int nNumReflectionModels = pPVS->GetNumActiveReflectionCityModels(nViewport);

			for	(nI = 0; nI < nNumReflectionModels; nI++)
			{
				mcCityModel	*pModel = pPVS->GetActiveReflectionCityModel(nViewport, nI);
				if (pModel)
					pModel->AddAreaLight();
			}

			MCCITY->GetEnvMap()->RenderLights();
		}
	}
	else
	{
		mcCullableClass::RenderAllTypes(ePassMask);
	}

	static bool s_cityLog = ARGS.Get("citylog") != NULL;
	static int s_censusTicks = 0;
	if (s_cityLog && ePassMask == dtMAIN_SHADOWED && (++s_censusTicks % 60 == 1)) {
		const Vector3 &cam = RSTATE.GetCameraPosition();
		Displayf("mcCityModelClass census: ground=%d main=%d reflect=%d HDR=%d alpha=%d (PVS=%d) cam (%.1f %.1f %.1f) dist %.0f: "
		         "active %d, rejected dist %d clip %d occl %d, rendered %d (no main model %d)",
		         m_nNumPartGround, m_nNumPartMain, m_nNumPartReflect, m_nNumPartHDR, m_nNumPartAlpha,
		         (MCCITY->UsePVS() && pPVS) ? 1 : 0, cam.x, cam.y, cam.z, MCCITY->GetNoPVSDrawDist(),
		         g_cityCull.active, g_cityCull.rejDist, g_cityCull.rejClip, g_cityCull.rejOccl, g_cityCull.rendered, g_cityCull.noModel);
	}
	if (ePassMask == dtMAIN_SHADOWED) { g_cityCull.Reset(); g_cityCull.logNames = s_cityLog && (s_censusTicks % 60 == 0) && s_censusTicks <= 120; }

}

mcCityModel::mcCityModel	(void)
{
	mcCityModelClass::CreateInstance();

	m_nGroup = 0;
}

mcCityModel::~mcCityModel	(void)
{
	if	(m_pType)
	{
		m_pType->Release();
		m_pType = NULL;
	}

	mcCityModelClass::Release();
}

bool	mcCityModel::LoadModels	(const char *pName)
{
	mcCullable::Init();

	m_pType = mcCityModelClass::GetInstance()->LoadType((mcCityModelType *)0, pName, "cc");

	m_fRadius = m_pType->GetRadius();

	return(m_pType ? true : false);
}

void	mcCityModel::Render	(mcPassTypes ePassMask)
{
	PF_START(CityModel);

	if	(!MCCITY->UsePVS())
	{
		bool clipPass = SetClippingAndSphereTest();
		float fDist = GetCenter().Dist(*(Vector3 *)&rmcState::GetCamera().d) - GetRadius();
		bool distPass = fDist <= MCCITY->GetNoPVSDrawDist();

		if	(!clipPass || !distPass)
		{
			if (ePassMask == dtMAIN_SHADOWED) {
				if (!distPass) g_cityCull.rejDist++;
				else if (PIPE.GetViewport()->FastSphereVisCheck(Vector4(GetCenter().x, GetCenter().y, GetCenter().z, GetRadius())) == cullOutside) g_cityCull.rejClip++;
				else g_cityCull.rejOccl++;
			}
			PF_STOP(CityModel);
			return;
		}
	}
	if (ePassMask == dtMAIN_SHADOWED) {
		g_cityCull.rendered++;
		if (!GetCityModelType() || !GetCityModelType()->GetModel(GetLOD(), partMAIN)) g_cityCull.noModel++;
		if (g_cityCull.logNames && GetCityModelType()) {
			mcCityModelType *t = GetCityModelType();
			const rmcModel *mainM = t->GetModel(GetLOD(), partMAIN), *gndM = t->GetModel(GetLOD(), partGROUND);
			Vector3 bmin(0,0,0), bmax(0,0,0);
			if (mainM) mainM->GetBoundingBox(bmin, bmax); else if (gndM) gndM->GetBoundingBox(bmin, bmax);
			bmin.Add(GetCenter()); bmax.Add(GetCenter());
			Displayf("  city render: %-34s lod %d group %d center (%.0f %.0f %.0f) r %.0f dist %.0f main %s(%d pk) ground %s(%d pk) world box (%.0f %.0f %.0f)-(%.0f %.0f %.0f)",
			         t->GetName(), GetLOD(), GetGroup(), GetCenter().x, GetCenter().y, GetCenter().z, GetRadius(),
			         GetCenter().Dist(RSTATE.GetCameraPosition()), mainM ? "yes" : "NO", mainM && mainM->GetModel(0) ? const_cast<rmcModel *>(mainM)->GetModel(0)->packets.GetCount() : 0,
			         gndM ? "yes" : "NO", gndM && gndM->GetModel(0) ? const_cast<rmcModel *>(gndM)->GetModel(0)->packets.GetCount() : 0,
			         bmin.x, bmin.y, bmin.z, bmax.x, bmax.y, bmax.z);
		}
	}

	Matrix34 s_Matrix = Matrix34::I;
	if (GetCityModelType())
	{
		s_Matrix.d = GetCityModelType()->GetCenter();
	}
	rmcState::SetWorldFast(s_Matrix);

	GetCityModelType()->Render(*this, ePassMask, 0);

	PF_STOP(CityModel);
}

void	mcCityModel::AddAreaLight	(void)
{
	PF_START(CityModel);

	if	(GetCityModelType())
	{
		const	rmcModel	*pModel = GetCityModelType()->GetModel(1, partMAIN);

		if	(pModel)
		{
			Matrix34 m = Matrix34::I;
			m.d = GetCityModelType()->GetCenter();

			MCCITY->GetEnvMap()->AddAreaLight(pModel, 0, m, this, m_nGroup);
		}
	}

	PF_STOP(CityModel);
}

void	mcCityModel::InitExtents	(const Vector3 &emin, const Vector3 &emax)
{
	int	x, z;

	MCCITY->CalcRelCellXZ(emin, x, z);

	m_nCellMinX = (u8)x;
	m_nCellMinZ = (u8)z;

	MCCITY->CalcRelCellXZ(emax, x, z);

	m_nCellMaxX = (u8)x;
	m_nCellMaxZ = (u8)z;
}

mcCityModelType::mcCityModelType	(void)	:	mcCullableType()
{
	SetClass(mcCityModelClass::GetInstance());
}

mcCityModelType::~mcCityModelType	(void)
{
}

void	mcCityModelType::LoadTypeData	(const char *pName, const char *pExt)
{
	m_ModelInfo.Load(pName, pExt);
}

void	mcCityModelType::RenderAllInstances	(mcPassTypes ePassMask)
{
	mcCullable	*pInstance = m_pFirstActiveCullable;

	while	(pInstance)
	{
		bool	bDraw = true;

		if	(!MCCITY->GetPVS())
		{

			float	fDist2 = pInstance->GetCullCenter().Dist2(RSTATE.GetCameraPosition());

			if	(fDist2 > square(MCCITY->GetNoPVSDrawDist() + pInstance->GetRadius()))
				bDraw = false;
		}
		if (ePassMask == dtMAIN_SHADOWED) { g_cityCull.active++; if (!bDraw) g_cityCull.rejDist++; }

		if	(bDraw)
			pInstance->Render((mcPassTypes)ePassMask);

		pInstance = pInstance->GetNext();
	}
}

void	mcCityModelType::Render	(mcCityModel &rCityModel, mcPassTypes ePassMask, int nCpvIndex)
{
	const	rmcModel	*pModel;

	switch	(ePassMask)
	{
		case	dtCULLING:				break;

		case	dtREFLECTED_OBJECTS:	pModel = GetModel(0, partREFLECT);
										if	(pModel)
										{
											pModel->Draw(MCCITY->GetShaders(rCityModel.GetGroup()), NULL, 0, 0);
											BANK_ONLY(mcCityModelClass::GetInstance()->m_nNumPartReflect++);
										}
										break;

		case	dtREFLECTING_GROUND:	pModel = GetModel(rCityModel.GetLOD(), partGROUND);

										if	(pModel)
										{
											pModel->DrawCpv(MCCITY->GetShaders(rCityModel.GetGroup()), NULL, 0, 0, nCpvIndex);
											BANK_ONLY(mcCityModelClass::GetInstance()->m_nNumPartGround++);
										}
										break;

		case	dtMAIN_SHADOWED:		pModel = GetModel(rCityModel.GetLOD(), partMAIN);
										if	(pModel)
										{
                                            pModel->DrawCpv(MCCITY->GetShaders(rCityModel.GetGroup()), NULL, 0, 0, nCpvIndex);
											BANK_ONLY(mcCityModelClass::GetInstance()->m_nNumPartMain++);
										}

                                        pModel = GetModel(rCityModel.GetLOD(), partHDR);
										if	(pModel)
										{
											int prevLightMode = rmcState::GetLightingMode();
											rmcState::SetLightingMode(rmclmNone);
                                            pModel->Draw(MCCITY->GetShaders(rCityModel.GetGroup()), NULL, 0, 0);
											rmcState::SetLightingMode(prevLightMode);
											BANK_ONLY(mcCityModelClass::GetInstance()->m_nNumPartHDR++);
										}
										break;

		case	dtALPHA:				pModel = GetModel(rCityModel.GetLOD(), partALPHA);
										if	(pModel)
										{
											pModel->DrawCpv(MCCITY->GetShaders(rCityModel.GetGroup()), NULL, 0, 0, nCpvIndex);
											BANK_ONLY(mcCityModelClass::GetInstance()->m_nNumPartAlpha++);
										}
										break;

		default:						Assert(0 && "Rendering pass not handled!");
										break;
	}
}
