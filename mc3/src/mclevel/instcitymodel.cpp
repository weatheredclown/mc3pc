
#include	"mclevel/level.h"
#include	"mclevel/instcitymodel.h"
#include	"mclevel/pvs.h"
#include	"profile/page.h"
#include	"mcdata/globaloptions.h"
#include	"mcgfx/mcgfx.h"

#include	"mceffects/envmap.h"

#include	"rmcore/model.h"
#include	"rmcore/cpvpalette.h"

mcInstCityModelClass::mcInstCityModelClass	(void)
{

	mcCullableMgr::AddClass(m_eDrawOrder, this);
}

void	mcInstCityModelClass::SetRenderStates	(const mcPassTypes ePassMask) const
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

void	mcInstCityModelClass::RestoreRenderStates	(const mcPassTypes  ) const
{

	// PC port: see mcCityModelClass::RestoreRenderStates
	rmcState::SetBlendSet(rmcbsNormal);
	rmcState::SetLightingMode(rmclmNone);

	rmcTextureFactory::PopInstance();
}

void	mcInstCityModelClass::RenderAllTypes	(mcPassTypes ePassMask)
{
	mcPVS		*pPVS = MCCITY->GetPVS();
	const	int	nViewport = MCCITY->GetActiveViewport();
	int			nI;

	if	(MCCITY->UsePVS() && pPVS && nViewport >= 0 && nViewport < mc::MAX_VIEWPORTS)
	{
		const	int	nNumModels = pPVS->GetNumActiveInstModels(nViewport);

		if	(ePassMask == dtMAIN_UNSHADOWED)
		{
			for	(nI = nNumModels-1; nI >= 0; nI--)
			{
				mcInstCityModel *pModel = pPVS->GetActiveInstModel(nViewport, nI);
				if (pModel)
					pModel->Render(ePassMask);
			}
		}
		else
		{
			for	(nI = 0; nI < nNumModels; nI++)
			{
				mcInstCityModel *pModel = pPVS->GetActiveInstModel(nViewport, nI);
				if (pModel)
					pModel->Render(ePassMask);
			}
		}

		if	((ePassMask == dtMAIN_SHADOWED) && MCCITY->GetEnvMap() WIN32PC_ONLY(&& mcConfig::GetGlobalGameOptions().GetEnvironmentMappingOption()))
		{
				
			const int nNumReflectionModels = pPVS->GetNumActiveReflectionInstModels(nViewport);

			for	(nI = 0; nI < nNumReflectionModels; nI++)
			{
				mcInstCityModel * pModel = pPVS->GetActiveReflectionInstModel(nViewport, nI);
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

	static int s_instCensusTicks = 0;
	if (ePassMask == dtMAIN_SHADOWED && (++s_instCensusTicks % 60 == 1)) {
		const Matrix34 &cam = RSTATE.GetCamera();
		Displayf("mcInstCityModelClass census: ground=%d main=%d reflect=%d HDR=%d alpha=%d (PVS=%d) camPos=(%.1f %.1f %.1f) camFwd=(%.2f %.2f %.2f)",
		         m_nNumPartGround, m_nNumPartMain, m_nNumPartReflect, m_nNumPartHDR, m_nNumPartAlpha,
		         (MCCITY->UsePVS() && pPVS) ? 1 : 0,
		         cam.d.x, cam.d.y, cam.d.z, -cam.c.x, -cam.c.y, -cam.c.z);
	}

}

mcInstCityModel::mcInstCityModel	(void)
{
	mcInstCityModelClass::CreateInstance();

	m_nGroup = 0;
	m_nCPVIndex = 0;
	m_CullCenter.Set(0.0f, 0.0f, 0.0f);
	m_Matrix.Identity();
}

mcInstCityModel::~mcInstCityModel	(void)
{
	if	(m_pType)
	{
		m_pType->Release();
		m_pType = NULL;
	}

	mcInstCityModelClass::Release();
}

bool	mcInstCityModel::LoadModels	(const char *pType)
{
	mcCullable::Init();

	m_pType = mcInstCityModelClass::GetInstance()->LoadType((mcInstCityModelType *)0, pType, "cc");

	if	(!m_pType)
		return(false);

	m_fRadius = m_pType->GetRadius();

	return(m_pType ? true : false);
}

void	mcInstCityModel::AddAreaLight	(void)
{
	PF_START(InstCityModel);

	if	(GetInstCityModelType())
	{
		const	rmcModel	*pModel = GetInstCityModelType()->GetModel(1, partMAIN);

		if	(pModel)
		{
			MCCITY->GetEnvMap()->AddAreaLight(pModel, m_nCPVIndex, m_Matrix, this, m_nGroup);
		}
	}
	PF_STOP(InstCityModel);
}

void	mcInstCityModel::Render	(mcPassTypes ePassMask)
{
	PF_START(InstCityModel);

	if	(GetInstCityModelType())
	{

		if	(!MCCITY->UsePVS())
		{
			bool clipPass = SetClippingAndSphereTest();
			float fDist = GetCullCenter().Dist(*(Vector3 *)&rmcState::GetCamera().d) - GetRadius();
			bool distPass = fDist <= MCCITY->GetNoPVSDrawDist();

			if	(!clipPass || !distPass)
			{
				PF_STOP(InstCityModel);
				return;
			}
		}

		rmcState::SetWorldFast(m_Matrix);

		GetInstCityModelType()->Render(*this, ePassMask, m_nCPVIndex);
	}

	PF_STOP(InstCityModel);
}

mcInstCityModelType::mcInstCityModelType	(void)	:	mcCityModelType()
{
	SetClass(mcInstCityModelClass::GetInstance());
}

mcInstCityModelType::~mcInstCityModelType	(void)
{
}

void	mcInstCityModelType::Render	(mcInstCityModel &rInstCityModel, mcPassTypes ePassMask, int nCpvIndex)
{
	const	rmcModel	*pModel;

	switch	(ePassMask)
	{
		case	dtCULLING:				break;

		case	dtREFLECTED_OBJECTS:	pModel = GetModel(0, partREFLECT);
										if	(pModel)
										{
											pModel->Draw(MCCITY->GetShaders(rInstCityModel.GetGroup()), NULL, 0, 0);
											BANK_ONLY(mcInstCityModelClass::GetInstance()->m_nNumPartReflect++);
										}
										break;

		case	dtREFLECTING_GROUND:	pModel = GetModel(rInstCityModel.GetLOD(), partGROUND);
										if	(pModel)
										{
											pModel->DrawCpv(MCCITY->GetShaders(rInstCityModel.GetGroup()), NULL, 0, 0, nCpvIndex);
											BANK_ONLY(mcInstCityModelClass::GetInstance()->m_nNumPartGround++);
										}
										break;

		case	dtMAIN_SHADOWED:		pModel = GetModel(rInstCityModel.GetLOD(), partMAIN);
										if	(pModel)
										{
                                            pModel->DrawCpv(MCCITY->GetShaders(rInstCityModel.GetGroup()), NULL, 0, 0, nCpvIndex);
											BANK_ONLY(mcInstCityModelClass::GetInstance()->m_nNumPartMain++);
										}

                                        pModel = GetModel(rInstCityModel.GetLOD(), partHDR);
										if	(pModel)
										{
											int prevLightMode = rmcState::GetLightingMode();
											rmcState::SetLightingMode(rmclmNone);
                                            pModel->Draw(MCCITY->GetShaders(rInstCityModel.GetGroup()), NULL, 0, 0);
											rmcState::SetLightingMode(prevLightMode);
											BANK_ONLY(mcInstCityModelClass::GetInstance()->m_nNumPartHDR++);
										}
										break;

		case	dtALPHA:				pModel = GetModel(rInstCityModel.GetLOD(), partALPHA);
										if	(pModel)
										{
											pModel->DrawCpv(MCCITY->GetShaders(rInstCityModel.GetGroup()), NULL, 0, 0, nCpvIndex);
											BANK_ONLY(mcInstCityModelClass::GetInstance()->m_nNumPartAlpha++);
										}
										break;

		default:						Assert(0 && "Rendering pass not handled!");
										break;
	}
}
