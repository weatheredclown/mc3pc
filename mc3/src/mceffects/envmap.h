#ifndef	MCLEVEL_CITYENVMAP_H
#define	MCLEVEL_CITYENVMAP_H

#include	"parse/fileio.h"

class	rmcModel;
class	mcCullable;

#define	TARGET_SIZE		128
#define	MAX_AREALIGHTS	256

struct	AreaLight
{
	const	rmcModel	*m_pModel;
	int					m_nCPVIndex;
	Matrix34			m_Matrix;
	mcCullable			*m_pCullable;
	float				m_fDistance;
	int					m_nGroup;
};

class mcCityEnvMap : public parFileIO
{
	private:
		rmcRenderTarget			*m_pTarget;
		rmcRenderTarget			*m_pZBuffer;
		rmcTexture				*m_pGlowTexture;
		rmcTexture				*m_staticEnvTex;
		rmcTexture				*m_pSphereMap;
		gfxViewport				*m_pVP;
		// PC port: the ortho viewport BeginRender clears the env map through
		// (the PS2 called it m_pViewport and kept it inside its own #if).
		gfxViewport				*m_pEnvClearVP;
		// PC port: the viewport in force before a target of ours was bound.
		gfxViewport				*m_pPrevVP;
		float					m_fFOV;

		rmcTexture				*m_pSkyTex;

		const	Vector3			*m_pFocus;

		Matrix34				m_mOldCam;
		Matrix34				m_cam;

		float					m_buildingAlpha;
		float					m_texLod;
		bool					m_bOldFog;

		int						m_nNumAreaLights;
		AreaLight				m_AreaLights[MAX_AREALIGHTS];

		bool					m_bSmoothTranslate;
		Vector4					m_clearColor;

	public:
				mcCityEnvMap		(void);
				~mcCityEnvMap		(void);
		void	SetupCam			(void);

		void	AddAreaLight		(const rmcModel *pModel, int nCPVIndex, const Matrix34 &Matrix, mcCullable *pCullable, int nGroup);
		void	RenderLights		(void);

		// PC port: -staticenvmap / -noenvmap turn the live city environment map off.
		static	bool	LiveEnvMapEnabled();
		// PC port: the gfx texture name the live target is published under.
		static	const char *GetLiveTextureName();
		// PC port: -noreflect turns the reflected-objects pass off for an A/B.

	public:

};

#endif
