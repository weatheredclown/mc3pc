#include	"data/string.h"
#include	"data/timemgr.h"
#include	"data/assetcfg.h"
#include	"rmcore/model.h"
#include	"rmcore/state.h"
#include	"snd_control/bank.h"
#include	"snd_fx/soundfx.h"
#include	"vector/random.h"
#include	"gfx/misc.h"
#include	"gfx/statetypes.h"
#include	"gfx/state.h"
#include	"data/parser.h"
#include	"vector/amath.h"

#include	"mclevel/skyhat.h"
#include	"mclevel/level.h"
#include	"mclevel/starfield.h"
#include	"mcdata/config.h"
#include	"snd/manager.h"
#include	"snd_control/volumegroup.h"
#include	"mcgfx/mcgfx.h"

#include	"mclevel/pvs.h"

#define	RAIN_SOUND_BANK	("rain")

mcSkyHatClass::mcSkyHatClass	(void)
{
	m_nNumLayers = 0;

	for	(int nI = 0; nI < MAX_LAYERS; nI++)
	{
		m_pModels[nI] = NULL;
		m_fYRotations[nI] = 0.0f;
		m_fYRotationSpeed[nI] = 0.0f;
	}

	m_pStars = NULL;
	m_pLightningSound = NULL;

	m_triggerLightning1 = false;
	m_triggerLightning2 = false;
	m_triggerLightning3 = false;
	m_lightningStartTime = 0.0f;
	m_lightningNextStart = 0.0f;
	m_lightningNum = 0;
	m_lightningGoing = false;
	m_lightningSeed = 69;
	m_lightningAudioDelay = 0.0f;

	m_topColorMod.Set(1.0f, 1.0f, 1.0f, 0.5f);
	m_bottomColorMod.Set(1.0f, 1.0f, 1.0f, 0.5f);

	m_clearColor.Set(0.0f);
	m_lightningStrength = 1.0f;
	m_boltDirection[0] = 1.17f;
	m_boltDirection[1] = 0.0f;
	m_boltDirection[2] = 0.0f;
	m_boltTop = 7.4f;
	m_boltBottom = 0.0f;
	m_boltDist = 35.0f;
	m_glowColor.Set(0.66f, 0.66f, 0.94f);
	m_vOffset.Zero();

	mcCullableMgr::AddClass(m_eDrawOrder, this);
}

mcSkyHatClass::~mcSkyHatClass	(void)
{
	SetName(NULL);

	if	(m_pLightningSound)
	{
		delete m_pLightningSound;
		SNDBANKMGR->UnloadBank(RAIN_SOUND_BANK);
		m_pLightningSound = NULL;
	}

	delete	m_pStars;

	for	(int nI = 0; nI < MAX_LAYERS; nI++)
	{
		if	(m_pModels[nI])
		{
			m_pModels[nI]->Delete();
			m_pModels[nI] = NULL;
		}
	}
}

void	mcSkyHatClass::SetRenderStates	(const mcPassTypes  )	const
{

	rmcState::SetFogBlend(false);
	rmcState::SetLightingMode(rmclmNone);

	rmcState::SetDepthWrite(false);

	// PC port: the frame's alpha channel is a mask, not a colour - the reflecting
	// ground writes 1 there so CopyReflectedObjectsToFrame can blend the
	// reflection through it, and the HDR pass writes 1 for the bloom.  The sky
	// draws in the same pass as the reflecting ground, and on D3D its texture
	// alpha went straight into that mask, so the city's reflection came out
	// across the sky.  RGB only leaves the mask alone.
	rmcState::SetColorWrite(rmccwRGB);
}

void	mcSkyHatClass::RestoreRenderStates	(const mcPassTypes  )	const
{

	rmcState::SetDepthWrite(true);
	rmcState::SetColorWrite(rmccwRGBA);   // PC port: see SetRenderStates
}

void	mcSkyHatClass::RenderAllTypes	(mcPassTypes )
{
	const Matrix34 *cam = MCCITY->GetCamera();

	// PC port: -noskyhat skips the sky entirely (diagnostic: the dome is only
	// ~100 m across and centred on the camera, so anything it draws over is a
	// depth-ordering bug rather than distance culling)
	static int sNoSkyhat = -1;
	if (sNoSkyhat < 0) sNoSkyhat = ARGS.Get("noskyhat") ? 1 : 0;
	if (sNoSkyhat) return;

	Draw(cam, m_topColorMod, true);
	Draw(cam, m_bottomColorMod, false);

}

float	mcSkyHatClass::GetLightningColor(Vector3 &color)	const
{
	float	u = 0.0f;

	if	(m_lightningGoing)
	{
		float	t = TIME.GetElapsedTime() - m_lightningStartTime;
		float	speed = 0.025f;

		if	(t < speed * PI * 3.0f)
			u = cosf(t * (1.0f / speed)) * 0.5f + 0.5f;
		else
			u = 0.0f;

		u *= m_lightningStrength;
	}

	color.Scale(m_glowColor, u);

	return(u);
}

void	mcSkyHatClass::Draw	(const Matrix34 *cam, const Vector4 &colorMod, bool bottom)	const
{
	Matrix34	Matrix;

	Matrix34 CameraMatrix = *cam;
	rmcState::SetDepthFunc(rmcdfCloserEqual);

	if	(m_pModels[0])
	{
		Vector3	a;

		rmcState::SetBlendSet(rmcbsOverwrite);
		rmcState::SetAlphaFunc(rmcafGreaterEqual);

		u8	prevAlphaRef = rmcState::GetAlphaRef();
		rmcState::SetAlphaRef(0);

		Matrix.Identity();
        float	fAngle = m_fYRotations[0] * 2.0f * PI / 360.0f;        
		Matrix.RotateY(fAngle);
        if (bottom)
            Matrix.RotateZ(PI);
        
		Matrix.d.Add(CameraMatrix.d, m_vOffset);
		rmcState::SetWorld(Matrix);        

		rmcState::SetColorWrite(rmccwRGBA);
		rmcState::SetForceColor(mkfrgba(colorMod));

		if	(mcConfig::IsRainy())
		{
			float	u = GetLightningColor(a);
			u = 0.5f + 0.5f * u;
			rmcState::SetBaseColor(mkfrgb(u,u,u));            
		}
		
	    m_pModels[0]->Draw(MCCITY->GetSkyShaders(), NULL, 0, 0);

		rmcState::SetAlphaRef(prevAlphaRef);
	}

	rmcState::SetAlphaBlend(true);

		rmcState::SetBlendSet(rmcBlendSetCustom_D3D(D3DBLEND_SRCALPHA, D3DBLEND_ONE));

	if	(m_lightningGoing)
	{
		float	t = TIME.GetElapsedTime() - m_lightningStartTime;
		float	u;
		float	speed = 0.025f;

		if	(t < speed * PI * 3.0f)
			u = cosf(t * (1.0f / speed)) * 0.5f + 0.5f;
		else
			u = 0.0f;

		u *= m_lightningStrength;

		if	(u > 1.0f)
			u = 1.0f;

		rmcState::SetBaseColor(mkfrgb(m_glowColor.x * u,m_glowColor.y * u,m_glowColor.z * u));

		if	(m_pModels[(m_lightningNum) * 2 + 1])
			m_pModels[(m_lightningNum) * 2 + 1]->Draw(MCCITY->GetSkyShaders(), NULL, 0, 0);

		Vector3	p1, p2;

		p1.Set(0.0f, m_boltTop, m_boltDist);
		p2.Set(0.0f, m_boltBottom, m_boltDist);

		p1.RotateY(m_boltDirection[m_lightningNum]);
		p2.RotateY(m_boltDirection[m_lightningNum]);

	}

	rmcState::SetBaseColor(mkfrgb(1,1,1));
}

void	mcSkyHatClass::FileIO	(datParser &Parser)
{
	for	(int nN = 0; nN < MAX_LAYERS; nN++)
	{
		char	nName[256];
		formatf(nName, sizeof(nName), "layer%d", nN);
		Parser.AddValue(nName, &m_fYRotationSpeed[nN]);
	}

	Parser.AddValue("m_clearColor", &m_clearColor);

	Parser.AddValue("m_lightningStrength", &m_lightningStrength);

	Parser.AddValue("m_boltDirection", &m_boltDirection[0]);
	Parser.AddValue("m_boltDirection1", &m_boltDirection[1]);
	Parser.AddValue("m_boltDirection2", &m_boltDirection[2]);

	Parser.AddValue("m_boltTop", &m_boltTop);
	Parser.AddValue("m_boltBottom", &m_boltBottom);
	Parser.AddValue("m_boltDist", &m_boltDist);
	Parser.AddValue("m_glowColor", &m_glowColor);

	Parser.AddValue("offset", &m_vOffset);
}

#include	"data/resourcehelpers.h"
