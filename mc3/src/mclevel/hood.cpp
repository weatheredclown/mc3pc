





#include	"mclevel/level.h"
#include	"mclevel/instcitymodel.h"






mcHood::mcHood	(void)
{
	m_pName = NULL;
	m_nNumCityModels = 0;
	m_ppCityModels = NULL;
	m_nNumInstCityModels = 0;
	m_ppInstCityModels = NULL;
}



mcHood::~mcHood	(void)
{
	DeleteModels();

	delete []	m_pName;
}



void	mcHood::Init	(const char *pName)
{
	m_pName = StringDuplicate(pName);
}



void	mcHood::DeleteModels	(void)
{
	int	nI;

	if (m_ppInstCityModels)
	{
		for	(nI = 0; nI < m_nNumInstCityModels; nI++)
		{
			delete	m_ppInstCityModels[nI];
		}

		delete []	m_ppInstCityModels;
		m_ppInstCityModels = NULL;
	}

	if (m_ppCityModels)
	{
		for	(nI = 0; nI < m_nNumCityModels; nI++)
		{
			delete	m_ppCityModels[nI];
		}

		delete []	m_ppCityModels;
		m_ppCityModels = NULL;
	}
}



void	mcHood::AllocCityModels	(const int nNumModels)
{
	int	nI;

	m_ppCityModels = new mcCityModel *[nNumModels];

	m_nNumCityModels = nNumModels;

	for	(nI = 0; nI < nNumModels; nI++)
	{
		m_ppCityModels[nI] = new mcCityModel;
	}
}



void	mcHood::AllocInstCityModels	(const int nNumModels)
{
	int	nI;

	m_ppInstCityModels = new mcInstCityModel *[nNumModels];

	m_nNumInstCityModels = nNumModels;

	for	(nI = 0; nI < nNumModels; nI++)
	{
		m_ppInstCityModels[nI] = new mcInstCityModel;
	}
}





