#include	"gfx/simple.h"
#include	"gfx/vgl.h"

#include	"mclevel/pvs.h"
#include	"mclevel/level.h"

bool	g_bUseOccluders = true;
bool	g_bDrawOccludedVolumes = false;
bool	g_bDrawActiveOccluders = false;
bool	g_bFreezeOccluderViews = false;
bool	g_bDrawAllOccluders = false;

mcPVS::mcPVS	(void)
{
	int	nI;

	m_nNumCityModels = 0;
	m_nNumInstCityModels = 0;
	m_ppCityModels = NULL;
	m_ppInstModels = NULL;
	m_pCityCellCache = NULL;
	m_pPVSData = NULL;
	m_ppCellPVSData = NULL;
	m_bValidPVS = false;

	m_NoPVSMinFrameTime = 34.0f;

	for	(nI = 0; nI < mc::MAX_VIEWPORTS; nI++)
	{
		m_CellOffsetTable[nI] = NULL;
		m_CellOffsetTableSize[nI] = 0;
		m_GeneratedNoPVSDistance[nI] = 10.0f;
		m_DistanceBasedPVSActive[nI] = false;
		m_VehicleSpeedLerp[nI] = 0.0f;
	}

	m_DoPVSHeightSwitch = true;

	m_MinNoPVSDistance = 650.0f;
	m_MaxNoPVSDistance = 1100.0f;

	m_bUsePVS = true;

	m_DistanceBasedSwitchHeight = 5.0f;

	for	(nI = 0; nI < mc::MAX_VIEWPORTS; nI++)
	{
		m_nNumActiveCityModels[nI] = 0;
		m_nNumActiveReflectionCityModels[nI] = 0;
		m_nNumActiveInstModels[nI] = 0;
		m_nNumActiveReflectionInstModels[nI] = 0;

		int	nModel;

		for	(nModel = 0; nModel < MAX_ACTIVE_CITY_MODELS; nModel++)
            m_pActiveCityModels[nI][nModel] = NULL;

		for	(nModel = 0; nModel < MAX_ACTIVE_INST_MODELS; nModel++)
			m_pActiveInstModels[nI][nModel] = NULL;

		for	(nModel = 0; nModel < MAX_ACTIVE_REFLECTION_INST_MODELS; nModel++)
		{
			m_pActiveReflectionCityModels[nI][nModel] = NULL;
			m_pActiveReflectionInstModels[nI][nModel] = NULL;
		}

		m_CurrentNoPVSDistance[nI] = 800.0f;
		m_BadFrameRateCount[nI] = 0;
	}

	m_NoPVSPullInStepSize = 40.0f;
	m_NoPVSPushOutStepSize = 10.0f;

	SetIntendedFrameRate(30);
	if (ARGS.Get("30hz"))
	{
		m_MinNoPVSDistance = 300.0f;
		m_MaxNoPVSDistance = 800.0f;
		m_bUsePVS = false;
	}

}

mcPVS::~mcPVS	(void)
{
	DeleteCityCellCache();

	if	(m_pPVSData)
		delete [] m_pPVSData;

	if	(m_ppCellPVSData)
		delete [] m_ppCellPVSData;

	for	(int nI = 0; nI < mc::MAX_VIEWPORTS; nI++)
		delete [] m_CellOffsetTable[nI];
}

bool	mcPVS::InitFromResource	(const datResourceImage &image, u32 pPVS,
								 const std::unordered_map<u32, mcCityModel*> &cityModels,
								 const std::unordered_map<u32, mcInstCityModel*> &instModels)
{
	if (!pPVS || !image.IsValidAddress(pPVS, 0x18))
		return false;

	m_nNumCityModels = (int)image.ReadU32(pPVS + 0x00);
	m_nNumInstCityModels = (int)image.ReadU32(pPVS + 0x04);
	u32 ppCityModelsAddr = image.ReadU32(pPVS + 0x08);
	u32 ppInstModelsAddr = image.ReadU32(pPVS + 0x0c);
	u32 pCityCellCacheAddr = image.ReadU32(pPVS + 0x10);
	u32 pPVSDataAddr = image.ReadU32(pPVS + 0x14);

	int numCells = MCCITY->GetNumCells();

	Displayf("mcPVS::InitFromResource: pPVS 0x%08x, city models %d, inst models %d, cells %d",
	         pPVS, m_nNumCityModels, m_nNumInstCityModels, numCells);

	u32 pvsBaseAddr = pPVS;
	u32 pvsBlockSize = (image.GetBase() + image.GetSize() > pvsBaseAddr)
		? (image.GetBase() + image.GetSize() - pvsBaseAddr)
		: 0;

	if (pvsBlockSize == 0 || !image.IsValidAddress(pvsBaseAddr, pvsBlockSize))
		return false;

	m_pPVSData = new u8[pvsBlockSize];
	memcpy(m_pPVSData, image.At(pvsBaseAddr), pvsBlockSize);

	auto ToLocal = [this, pvsBaseAddr, pvsBlockSize](u32 addr, u32 reqSize) -> const u8* {
		if (addr >= pvsBaseAddr && (addr - pvsBaseAddr + reqSize) <= pvsBlockSize)
			return m_pPVSData + (addr - pvsBaseAddr);
		return NULL;
	};

	if (m_nNumCityModels > 0 && ppCityModelsAddr && image.IsValidAddress(ppCityModelsAddr, (u32)m_nNumCityModels * 4))
	{
		m_ppCityModels = new mcCityModel*[m_nNumCityModels];
		int resolved = 0;
		for (int i = 0; i < m_nNumCityModels; i++)
		{
			u32 mAddr = image.ReadU32(ppCityModelsAddr + (u32)i * 4);
			auto it = cityModels.find(mAddr);
			if (it != cityModels.end())
			{
				m_ppCityModels[i] = it->second;
				resolved++;
			}
			else
			{
				m_ppCityModels[i] = NULL;
			}
		}
		Displayf("mcPVS: resolved %d/%d unique city models", resolved, m_nNumCityModels);
	}

	if (m_nNumInstCityModels > 0 && ppInstModelsAddr && image.IsValidAddress(ppInstModelsAddr, (u32)m_nNumInstCityModels * 4))
	{
		m_ppInstModels = new mcInstCityModel*[m_nNumInstCityModels];
		int resolved = 0;
		for (int i = 0; i < m_nNumInstCityModels; i++)
		{
			u32 mAddr = image.ReadU32(ppInstModelsAddr + (u32)i * 4);
			auto it = instModels.find(mAddr);
			if (it != instModels.end())
			{
				m_ppInstModels[i] = it->second;
				resolved++;
			}
			else
			{
				m_ppInstModels[i] = NULL;
			}
		}
		Displayf("mcPVS: resolved %d/%d instanced city models", resolved, m_nNumInstCityModels);
	}

	if (numCells > 0 && pCityCellCacheAddr && image.IsValidAddress(pCityCellCacheAddr, (u32)numCells * 12))
	{
		m_pCityCellCache = new mcPVSCellCache[numCells];
		int cellsWithModels = 0;
		for (int c = 0; c < numCells; c++)
		{
			u32 entryAddr = pCityCellCacheAddr + (u32)c * 12;
			u16 nCity = image.ReadU16(entryAddr);
			u16 nInst = image.ReadU16(entryAddr + 2);
			u32 pCityIdxAddr = image.ReadU32(entryAddr + 4);
			u32 pInstIdxAddr = image.ReadU32(entryAddr + 8);

			m_pCityCellCache[c].m_nNumCityModels = (short)nCity;
			m_pCityCellCache[c].m_nNumInstCityModels = (short)nInst;

			m_pCityCellCache[c].m_pCityModelIndices = (pCityIdxAddr != 0)
				? (const u16*)ToLocal(pCityIdxAddr, (u32)nCity * 2)
				: NULL;

			m_pCityCellCache[c].m_pInstModelIndices = (pInstIdxAddr != 0)
				? (const u16*)ToLocal(pInstIdxAddr, (u32)nInst * 2)
				: NULL;

			if (nCity > 0 || nInst > 0)
				cellsWithModels++;
		}
		Displayf("mcPVS: cell cache loaded (%d/%d cells populated)", cellsWithModels, numCells);
	}

	if (numCells > 0 && pPVSDataAddr && image.IsValidAddress(pPVSDataAddr, (u32)numCells * 4))
	{
		m_ppCellPVSData = new const u8*[numCells];
		int cellsWithPVS = 0;
		for (int c = 0; c < numCells; c++)
		{
			u32 cellAddr = image.ReadU32(pPVSDataAddr + (u32)c * 4);
			const u8 *ptr = ToLocal(cellAddr, 4);
			if (ptr)
			{
				m_ppCellPVSData[c] = ptr;
				cellsWithPVS++;
			}
			else
			{
				m_ppCellPVSData[c] = NULL;
			}
		}
		Displayf("mcPVS: cell PVS data mapped (%d/%d cells with PVS visibility)", cellsWithPVS, numCells);
	}

	m_bValidPVS = (m_ppCellPVSData != NULL);
	m_bUsePVS = true;
	m_DoPVSHeightSwitch = true;

	Displayf("mcPVS::InitFromResource complete, valid=%d, use=%d", (int)m_bValidPVS, (int)m_bUsePVS);
	return m_bValidPVS;
}

void	mcPVS::SetIntendedFrameRate	(const int fps)
{
	m_NoPVSMinFrameTime = 17.0f;
	m_NoPVSMaxFrameTime = 20.0f;

	if (ARGS.Get("30hz"))
	{
		m_NoPVSMinFrameTime = 16.0f;
		m_NoPVSMaxFrameTime = 17.0f;
	}

	if	(fps <= 0)
		return;

	const	float	fpsWarp = 60.0f / float(fps);
	m_NoPVSMinFrameTime *= fpsWarp;
	m_NoPVSMaxFrameTime *= fpsWarp;
}

void	mcPVS::DeleteCityCellCache	(void)
{
	if	(!m_pCityCellCache)
		return;

	delete []	m_pCityCellCache;
	m_pCityCellCache = NULL;

	delete []	m_ppCityModels;
	m_ppCityModels = NULL;
	delete []	m_ppInstModels;
	m_ppInstModels = NULL;
}
