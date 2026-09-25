#include	"rmcore/model.h"

#include	"mceffects/envmap.h"
#include	"mclevel/level.h"
#include	"mcdata/config.h"

// gfx/rgl.cpp - grabs the live backbuffer (see -reflectdump below).
#include	"gfx/gputimer.h"	// -gputime scopes

// PC port: the live city environment map - the sky dome, the lit building models
// and the light glows drawn into __envmap__ every frame, which is what the car
// paint and the city windows reflect.  Every non-PS2 body in this file was empty,
// so the port fell back to a static fx_car_viewer_envmap texture
// (mclevel/shaders.cpp).  -staticenvmap goes back to that; -noenvmap draws nothing
// into the target at all.
const char *mcCityEnvMap::GetLiveTextureName()
{
	return "__cityenvmap__";
}

bool mcCityEnvMap::LiveEnvMapEnabled()
{
	static const bool s_off = (ARGS.Get("noenvmap") != NULL) || (ARGS.Get("staticenvmap") != NULL);
	return !s_off;
}

void	mcCityEnvMap::SetupCam	(void)
{
	Matrix34	newCam;

	m_pVP->SetWindow(0, 0, TARGET_SIZE, TARGET_SIZE, 0.0f, 1.0f);
	m_pVP->Perspective(m_fFOV, 1.0f, 0.25f, 1000.0f);
	Matrix44 proj = m_pVP->GetProjection();
	m_pVP->SetProjection(proj);

	Vector3	focus;

	if	(m_pFocus)
		focus.Set(*m_pFocus);
	else
		focus.Set(0.0f);

	focus.y += 1.0f;

	newCam.LookAt(focus, *(Vector3 *)&m_mOldCam.d);
	m_cam = newCam;
	m_cam.b *= -1;
	m_cam.a *= -1;
}

void	mcCityEnvMap::AddAreaLight	(const rmcModel *pModel, int nCPVIndex, const Matrix34 &Matrix, mcCullable *pCullable, int nGroup)
{
	float	fDistance;

	if	(!m_pFocus)
		return;

	if	(m_nNumAreaLights < MAX_AREALIGHTS)
	{
		const	Vector3	&pt = pCullable->GetCullCenter();

		Matrix44 oldCam = RSTATE.GetCamera();
		RSTATE.SetCamera(m_cam);

		if	(m_pVP->IsSphereVisible(pt.x, pt.y, pt.z, pCullable->GetRadius()))
		{
			fDistance = m_pFocus->Dist(pt);

			{
				m_AreaLights[m_nNumAreaLights].m_pModel = pModel;
				m_AreaLights[m_nNumAreaLights].m_nCPVIndex = nCPVIndex;
				m_AreaLights[m_nNumAreaLights].m_Matrix = Matrix;
				m_AreaLights[m_nNumAreaLights].m_pCullable = pCullable;
				m_AreaLights[m_nNumAreaLights].m_fDistance = fDistance;
				m_AreaLights[m_nNumAreaLights].m_nGroup = nGroup;
				m_nNumAreaLights++;
			}
		}
		RSTATE.SetCamera(oldCam);
	}
}

void	mcCityEnvMap::RenderLights	(void)
{
	GPU_SCOPE("fx:EnvMap.Lights");

	if	(!LiveEnvMapEnabled() || !m_pTarget || !m_pZBuffer)
	{
		m_nNumAreaLights = 0;
		return;
	}

	SetupCam();

	gfxViewport *prevVP = PIPE.GetViewport();
	Matrix34 prevCam = rmcState::GetCamera();
	rmcTextureFactory::GetInstance().LockRenderTarget(m_pTarget, m_pZBuffer, m_pVP, m_cam);

	rmcState::SetDepthFunc(rmcdfCloserEqual);

	m_bOldFog = RSTATE.GetFogEnable();
	RSTATE.SetFogEnable(false);

	rmcState::SetLightingMode(rmclmNone);
	rmcState::SetBaseColor(mkrgba(255, 255, 255, 0));
	rmcState::SetBlendSet(rmcBlendSetCustom_D3D(D3DBLEND_ONE, D3DBLEND_ZERO));
	rmcState::SetAlphaFunc(rmcafAlways);
	rmcState::SetAlphaRef(0);
	rmcState::SetAlphaBlend(false);

	rmcState::SetDepthWrite(true);
	rmcState::SetDepthTest(true);

	// one viewport, always: the split-screen case a race could ask for has no viewer equivalent
	{
		{
			rmcTextureFactory::GetInstance().SetTextureLod(m_texLod);

			for	(int nI = 0; nI < m_nNumAreaLights; nI++)
			{
				{
					rmcState::SetWorld(m_AreaLights[nI].m_Matrix);

					if	(m_AreaLights[nI].m_nCPVIndex >= 0)
						m_AreaLights[nI].m_pModel->DrawCpv(MCCITY->GetShaders(m_AreaLights[nI].m_nGroup), NULL, 0, 0, m_AreaLights[nI].m_nCPVIndex);
					else
						m_AreaLights[nI].m_pModel->Draw(MCCITY->GetShaders(m_AreaLights[nI].m_nGroup), NULL, 0, 0);
				}
			}
			rmcTextureFactory::GetInstance().SetTextureLod(0);

		}
	}

	rmcTextureFactory::GetInstance().UnlockRenderTarget();
	if	(prevVP)
		PIPE.SetViewport(prevVP);
	rmcState::SetCamera(prevCam);

	RSTATE.SetFogEnable(m_bOldFog);
	rmcState::SetDepthWrite(true);
	rmcState::SetDepthFunc(rmcdfCloserEqual);
	rmcState::SetAlphaBlend(true);
	rmcState::SetAlphaFunc(rmcafGreater);

	rmcState::SetWorld(Matrix34::I);
	m_nNumAreaLights = 0;
}
