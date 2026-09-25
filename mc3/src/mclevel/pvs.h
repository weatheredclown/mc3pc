#ifndef	MCLEVEL_PVS_H
#define	MCLEVEL_PVS_H

namespace mc {
	// One viewport: the split screen the game could ask for has no viewer equivalent.
	const int MAX_VIEWPORTS=1;
};

#include	"mcdata/config.h"
#include	<unordered_map>

class	mcCityModel;
class	mcInstCityModel;

class	mcPVSCellCache
{
	public:
		short			m_nNumCityModels;
		short			m_nNumInstCityModels;
		const u16		*m_pCityModelIndices;
		const u16		*m_pInstModelIndices;

	public:
						mcPVSCellCache	(void)
						{
							m_nNumCityModels = 0;
							m_nNumInstCityModels = 0;
							m_pCityModelIndices = NULL;
							m_pInstModelIndices = NULL;
						}

						mcPVSCellCache	(class datResource &rsc);
};

class	mcPVS
{
	private:
		enum	{ MAX_ACTIVE_CITY_MODELS = 1000 };
		enum	{ MAX_ACTIVE_INST_MODELS = 8000 };
		enum	{ MAX_ACTIVE_REFLECTION_CITY_MODELS = 500 };
		enum	{ MAX_ACTIVE_REFLECTION_INST_MODELS = 1500 };

		int				m_nNumCityModels;
		int				m_nNumInstCityModels;
		mcCityModel		**m_ppCityModels;
		mcInstCityModel	**m_ppInstModels;
		mcPVSCellCache	*m_pCityCellCache;
		u8				*m_pPVSData;
		const u8		**m_ppCellPVSData;
		u16				m_TempModelList[MAX_ACTIVE_INST_MODELS];
		bool			m_bValidPVS;
		s8				*m_CellOffsetTable[mc::MAX_VIEWPORTS];
		int				m_CellOffsetTableSize[mc::MAX_VIEWPORTS];
		float			m_GeneratedNoPVSDistance[mc::MAX_VIEWPORTS];
		bool			m_DistanceBasedPVSActive[mc::MAX_VIEWPORTS];
		float			m_VehicleSpeedLerp[mc::MAX_VIEWPORTS];
		bool			m_DoPVSHeightSwitch;
		float			m_MinNoPVSDistance;
		float			m_MaxNoPVSDistance;
		bool			m_bUsePVS;
		int				m_nViewCellX[mc::MAX_VIEWPORTS];
		int				m_nViewCellZ[mc::MAX_VIEWPORTS];
		float			m_DistanceBasedSwitchHeight;
		int				m_nNumActiveCityModels[mc::MAX_VIEWPORTS];
		int				m_nNumActiveReflectionCityModels[mc::MAX_VIEWPORTS];
		int				m_nNumActiveInstModels[mc::MAX_VIEWPORTS];
		int				m_nNumActiveReflectionInstModels[mc::MAX_VIEWPORTS];
		mcCityModel		*m_pActiveCityModels[mc::MAX_VIEWPORTS][MAX_ACTIVE_CITY_MODELS];
		mcCityModel		*m_pActiveReflectionCityModels[mc::MAX_VIEWPORTS][MAX_ACTIVE_REFLECTION_INST_MODELS];
		mcInstCityModel	*m_pActiveInstModels[mc::MAX_VIEWPORTS][MAX_ACTIVE_INST_MODELS];
		mcInstCityModel	*m_pActiveReflectionInstModels[mc::MAX_VIEWPORTS][MAX_ACTIVE_REFLECTION_INST_MODELS];
		float			m_CurrentNoPVSDistance[mc::MAX_VIEWPORTS];
		float			m_NoPVSMinFrameTime;
		float			m_NoPVSMaxFrameTime;
		int				m_BadFrameRateCount[mc::MAX_VIEWPORTS];
		float			m_NoPVSPullInStepSize;
		float			m_NoPVSPushOutStepSize;

		float			m_DebugSwitchCellHeight;
		bool			m_bNoDistancePVS;

	public:
						mcPVS								(void);
						mcPVS								(class datResource &rsc);
						~mcPVS								(void);
		bool			Load								(const char *pName);
		bool			InitFromResource					(const class datResourceImage &image, u32 pPVS,
															 const std::unordered_map<u32, mcCityModel*> &cityModels,
															 const std::unordered_map<u32, mcInstCityModel*> &instModels);
		void			Reset								(void);
		void			SetIntendedFrameRate				(const int fps);

		const	int		GetNumActiveCityModels				(const int nViewport) const	{ return(m_nNumActiveCityModels[nViewport]); }
		const	int		GetNumActiveReflectionCityModels	(const int nViewport) const	{ return(m_nNumActiveReflectionCityModels[nViewport]); }
		const	int		GetNumActiveInstModels				(const int nViewport) const	{ return(m_nNumActiveInstModels[nViewport]); }
		const	int		GetNumActiveReflectionInstModels	(const int nViewport) const	{ return(m_nNumActiveReflectionInstModels[nViewport]); }
		mcCityModel		*GetActiveCityModel					(const int nViewport, const int nI)	{ return(m_pActiveCityModels[nViewport][nI]); }
		mcCityModel		*GetActiveReflectionCityModel		(const int nViewport, const int nI)	{ return(m_pActiveReflectionCityModels[nViewport][nI]); }
		mcInstCityModel	*GetActiveInstModel					(const int nViewport, const int nI)	{ return(m_pActiveInstModels[nViewport][nI]); }
		mcInstCityModel	*GetActiveReflectionInstModel		(const int nViewport, const int nI)	{ return(m_pActiveReflectionInstModels[nViewport][nI]); }

	private:
		void			DeleteCityCellCache					(void);
};

#endif
