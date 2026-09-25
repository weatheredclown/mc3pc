#ifndef	MC_CITY_H
#define	MC_CITY_H

#include	"rmcore/shader.h"
#include	"rmcore/resourceversions.h"

#include	"mclevel/hood.h"
#include	"mclevel/skyhat.h"

class	mcCityEnvMap;
class	mcCityModelClass;
class	mcInstCityModelClass;
class	mcPVS;
class	Matrix34;
class	rmcCpvPalette;
class	rmcTextureFactory;

#define	CITY_RESOURCE_VERSION	(19 + rmcResourceBaseVersion)

class	mcCity	:	public	Base
{
	private:
		bool						m_bResourced;
		int							m_nNumShaderGroups;
		rmcShaderGroup				*m_pShaderGroups;
		static	rmcShaderGroup		*s_pShaderGroups;
		rmcShaderGroup				*m_pSkyShaderGroup;
		rmcShaderGroup				*m_pPropShaderGroup;
		int							m_nNumHoods;	
		mcHood						*m_pHoods;
		mcPVS						*m_pPVS;
		bool						m_bUsePVS;
		float						m_fNoPVSDrawDist;
		rmcCpvPalette				*m_pCPVPalette;
		rmcCpvPalette				*m_pCPVPaletteCopy;
		mcConditionVariables		m_CurrentCondition;
		mcSkyHatClass				*m_pSkyHat;
		mcCityModelClass			*m_pCityModelClass;
		mcInstCityModelClass		*m_pInstCityModelClass;
		const	Matrix34			*m_pCamera;
		int							m_nActiveViewport;
		Vector3						m_vAmbientLightingColor;
		Vector3						m_vDirectionalLightingColor;
		Vector3						m_vDirectionalLightingDirection;
		mcCityEnvMap				*m_pEnvMap;

		int							m_nAbsCameraCellX;
		int							m_nAbsCameraCellZ;
		bool						m_bDrawExtents;

	protected:
		Vector3						m_vExtentsMin;
		Vector3						m_vExtentsMax;
		int							m_nNumCells;
		int							m_nNumCellsX;
		int							m_nNumCellsZ;
		int							m_nCellMinX;
		int							m_nCellMinZ;
		static float				s_fCellSize;

	public:
		// PC port: builder for reading console packs
		friend struct datResourceBuilder<mcCity>;

									mcCity						(void);
									mcCity						(class datResource &rsc);
									DECLARE_PLACE(mcCity);
									~mcCity						(void);
		void						Load						(const char *pName);
		void						Delete						(void);
		void						Reset						(void);
		void						Update						(void);
		void						CalcRelCellXZ				(const Vector3 &vPos, int &nX, int &nZ) const;
		static	void				CalcAbsCellXZ				(const Vector3 &vPos, int &nX, int &nZ);
		void						SetGlobalRmLighting			(rmcLightGroup *rmLights, int gfxIdx, float mod = 1.0f);
		bool						IsSphereVisible				(int nView, const Vector3 &vCenter, float fRadius) const;
		void						SetupFog					(bool windowPass);

		rmcShaderGroup				&GetShaders					(int nGroup) { return(m_pShaderGroups[nGroup]); }
		rmcShaderGroup				&GetSkyShaders				(void) { return(*m_pSkyShaderGroup); }
		mcPVS						*GetPVS						(void) const { return(m_pPVS); }
		// PC port: enough of the built city to walk and draw it without the game's per-frame
		// machinery, which is not part of this tree (rscview -loadcity).
		int							GetNumHoods					(void) const { return(m_nNumHoods); }
		const	mcHood				&GetHood					(int nHood) const { return(m_pHoods[nHood]); }
		mcCityModelClass			*GetCityModelClass			(void) const { return(m_pCityModelClass); }
		mcInstCityModelClass		*GetInstCityModelClass		(void) const { return(m_pInstCityModelClass); }
		void						SetUsePVS					(bool bUse) { m_bUsePVS = bUse; }
		void						SetNoPVSDrawDist			(float fDist) { m_fNoPVSDrawDist = fDist; }
		bool						UsePVS						(void) const { return(GetPVS() && m_bUsePVS); }
		float						GetNoPVSDrawDist			(void) const { return(m_fNoPVSDrawDist); }
		void						SetIntendedFrameRate(const int fps);

		mcCityEnvMap				*GetEnvMap					(void) const { return(m_pEnvMap); }

		rmcCpvPalette				*GetCPVPalette				(void) const { return(m_pCPVPalette); }

		mcConditionVariables		&GetCurrentCondition		(void) { return(m_CurrentCondition); }
		const	Matrix34			*GetCamera					(void) const { return(m_pCamera); }
		int							GetActiveViewport			(void) const { return(m_nActiveViewport); }
		int							GetNumCells					(void) const { return(m_nNumCells); }
		static	float				GetCellSize					(void) { return(s_fCellSize); }

	private:

	public:
	private:

};

extern	mcCity	*MCCITY;

// PC port: console city pack builder declaration
template <> mcCity *datResourceBuilder<mcCity>::Build(const datResourceImage &image);

#endif
