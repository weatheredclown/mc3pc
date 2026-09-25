#ifndef	MCLEVEL_CONDITIONS_H
#define	MCLEVEL_CONDITIONS_H

#include	"parse/fileio.h"
#include	"vector/vector3.h"
#include	"vector/vector4.h"

class	mcConditionVariables	:	public	parFileIO
{
	public:
		Vector3	mFogColor;
		float	mFogStart;
		float	mFogEnd;
		float	mFogThreshold;
		float	mFogClamp;
		float	mLightFogScale;	

		Vector3	mIndoorFogColor;
		float	mIndoorFogStart;
		float	mIndoorFogEnd;
		float	mIndoorFogThreshold;
		float	mIndoorFogClamp;

		float	mAmbientTweakConstant;
		float	mAmbientTweakRadius;

		float	mAmbientVehicleModifier;

		Vector4	mReflBackgroundColor;
		float	mReflDistance;

		bool	mDrawLightGlows;
		float	mLightGlowIntensity;
		float	mLightGlowSize;

		bool	mDrawLightFlares;
		float	mLightFlareIntensity;
		float	mLightFlareSize;
		float	mLightFlareDistance;

		bool	mDrawLightCones;

		Vector4	mAmbientLight;
		Vector4	mDirectionalLightColor;

		float	mDirectionalRot;
		float	mDirectionalElevation;
		bool	mDrawGlobalLightPos;

		float m_skylightRot;
		float m_skylightElevation;
		bool m_drawSkylightPos;
		Vector4 m_skylightColor;
		bool m_useSkylight;

		float	mLightAttenuation;
		float	mLightFalloff;

		float	mGammaExp1;
		float	mGammaExp2;

	public:
				mcConditionVariables	(void);
				mcConditionVariables	(class datResource &rsc);
		void	Init					(const char *pName);
		void	Kill					(void);
		void	FileIO					(datParser &p);
		void	GetFogColor				(float t, Vector3 &v) { v.Lerp(t, mFogColor, mIndoorFogColor); }
		float	GetFogStart				(float t) { return(Lerp(t, mFogStart, mIndoorFogStart)); }
		float	GetFogEnd				(float t) { return(Lerp(t, mFogEnd, mIndoorFogEnd)); }
		float	GetFogClamp				(float t) { return(Lerp(t, mFogClamp, mIndoorFogClamp)); }
		Vector3 GetGlobalLightingDir	();

};

#endif
