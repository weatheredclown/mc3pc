
#include	"mclevel/conditions.h"

mcConditionVariables::mcConditionVariables	(void)
{
	mFogColor.Set(1.0f, 1.0f, 1.0f);
	mFogStart = 200.0f;
	mFogEnd = 300.0f;
	mFogThreshold = 0.0f;
	mFogClamp = 1.0f;

	mIndoorFogColor.Set(1.0f, 1.0f, 1.0f);
	mIndoorFogStart = 200.0f;
	mIndoorFogEnd = 300.0f;
	mIndoorFogThreshold = 0.0f;
	mIndoorFogClamp = 1.0f;

	mAmbientTweakConstant = 0.2f;
	mAmbientTweakRadius = 40.0f;

	mLightFogScale = 1.0f;

	mReflBackgroundColor.Set(0.0f, 0.0f, 0.0f, 1.0f);
	mReflDistance = 200.0f;

	mDrawLightGlows = true;
	mLightGlowIntensity = 0.7f;
	mLightGlowSize = 10.0f;

	mDrawLightFlares = true;
	mLightFlareIntensity = 0.5f;
	mLightFlareSize = 10.0f;
	mLightFlareDistance = 70.0f;

	mDrawLightCones = true;

	mAmbientLight.Set(0.1f, 0.1f, 0.1f, 0.0f);
	mDirectionalLightColor.Set(0.1f, 0.1f, 0.1f, 0.0f);

	mLightAttenuation = 1.0f;
	mLightFalloff = 1.0f;

	mDirectionalElevation = 2.0f;
	mDirectionalRot = 2.0f;
	mDrawGlobalLightPos = false;

	m_skylightRot = 0.0f;
	m_skylightElevation = 90.0f;
	m_drawSkylightPos = false;
	m_skylightColor.Set(0.2f, 0.2f, 0.23f, 1.0f);
	m_useSkylight = false;

	mAmbientVehicleModifier = 1.0f;

	mGammaExp1 = 1.070f;
	mGammaExp2 = 1.000f;
};

void	mcConditionVariables::FileIO	(datParser &p)
{
	p.AddValue("fog_color", &mFogColor);
	p.AddValue("fog_start", &mFogStart);
	p.AddValue("fog_end", &mFogEnd);
	p.AddValue("fog_threshold", &mFogThreshold);
	p.AddValue("fog_clamp", &mFogClamp);
	p.AddValue("lightfog_scale",&mLightFogScale);

	p.AddValue("indoor_fog_color", &mIndoorFogColor);
	p.AddValue("indoor_fog_start", &mIndoorFogStart);
	p.AddValue("indoor_fog_end", &mIndoorFogEnd);
	p.AddValue("indoor_fog_threshold", &mIndoorFogThreshold);
	p.AddValue("indoor_fog_clamp", &mIndoorFogClamp);

	p.AddValue("mAmbientTweakRadius", &mAmbientTweakRadius);
	p.AddValue("mAmbientTweakConstant", &mAmbientTweakConstant);

	p.AddValue("mAmbientVehicleModifier", &mAmbientVehicleModifier);

	p.AddValue("refl_bk_color", &mReflBackgroundColor);
	p.AddValue("refl_dist", &mReflDistance);

	p.AddValue("light_glow_intensity", &mLightGlowIntensity);
	p.AddValue("light_glow_size", &mLightGlowSize);

	p.AddValue("flare_on", &mDrawLightFlares);
	p.AddValue("flare_intensiry", &mLightFlareIntensity);
	p.AddValue("flare_size", &mLightFlareSize);
	p.AddValue("flare_distance", &mLightFlareDistance);

	p.AddValue("mAmbientLight", &mAmbientLight);
	p.AddValue("mDirectionalRot", &mDirectionalRot);
	p.AddValue("mDirectionalElevation", &mDirectionalElevation);
	p.AddValue("mDirectionalLightColor", &mDirectionalLightColor);

	p.AddValue("m_skylightRot", &m_skylightRot);
	p.AddValue("m_skylightElevation", &m_skylightElevation);
	p.AddValue("m_drawSkylightPos", &m_drawSkylightPos);
	p.AddValue("m_skylightColor", &m_skylightColor);
	p.AddValue("m_useSkylight", &m_useSkylight);

	p.AddValue("mLightAttenuation", &mLightAttenuation);
	p.AddValue("mLightFalloff", &mLightFalloff);

	p.AddValue("GammaExp1", &mGammaExp1);
	p.AddValue("GammaExp2", &mGammaExp2);
}

Vector3 mcConditionVariables::GetGlobalLightingDir()
{
	Vector3 dir(1.0f, 0.0f, 0.0f);
	dir.RotateZ(mDirectionalElevation * DtoR);
	dir.RotateY(mDirectionalRot * DtoR);
	return -dir;
}
