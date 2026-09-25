#include	"rmcore/model.h"



#include	"mclevel/citymodelinfo.h"



mcCityModelInfo::mcCityModelInfo	(void)
{
	m_vCenter.Zero();
	m_fRadius = 0.0f;

	for	(unsigned int nLod = 0; nLod < NUM_CITY_LODS; nLod++)
		for	(unsigned int nPart = 0; nPart < partNUMPARTS; nPart++)
			m_pModels[nLod][nPart] = NULL;
}



mcCityModelInfo::~mcCityModelInfo	(void)
{
	for	(unsigned int nLod = 0; nLod < NUM_CITY_LODS; nLod++)
		for	(unsigned int nPart = 0; nPart < partNUMPARTS; nPart++)
			if	(m_pModels[nLod][nPart])
				m_pModels[nLod][nPart]->Delete();
}



void	mcCityModelInfo::Load	(const char *pName, const char *pExt)
{
	char	nToken[256];
	bool	bInstCPVs = false;

	Stream	*S = ASSET.Open(pName, pExt);

	if	(!S)
	{
		Errorf("could not load city model: %s.%s", pName, pExt);
		return;
	}

	datTokenizer	T(pName, S);

	T.GetToken(nToken, 256);

	while	(nToken[0])
	{
		if	(strcmp(nToken, "inst_cpv:") == 0)
		{
			bInstCPVs = T.GetInt() ? true : false;
			Assert(bInstCPVs);
		}
		else if	(strcmp(nToken, "bounding_sphere:") == 0)
		{
			T.GetVector(m_vCenter);
			m_fRadius = T.GetFloat();
		}
		else if	(strcmp(nToken, "lod") == 0)
		{
			unsigned	int	nLod = T.GetInt();

			T.MatchToken("{");
			T.GetToken(nToken, 256);

			while	(strcmp(nToken, "}"))
			{
				mcBuildingParts	ePart = partNUMPARTS;
				rmcModel		*pModel = NULL;
				char			nModelName[256];

				if	(strcmp(nToken, "main") == 0)
				{
					ePart = partMAIN;
				}
				else if	(strcmp(nToken, "reflect") == 0)
				{
					ePart = partREFLECT;
				}
				else if	(strcmp(nToken, "ground") == 0)
				{
					ePart = partGROUND;
				}
				else if (strcmp(nToken, "hdr") == 0)
				{
					ePart = partHDR;
				}
				else if	(strcmp(nToken, "alpha") == 0)
				{
					ePart = partALPHA;
				}

				formatf(nModelName, sizeof(nModelName), "%s_%d_%s.mesh", pName, nLod, nToken);
				pModel = rmcModel::Create(nModelName);

				if	(pModel)
				{
					Assert(nLod < NUM_CITY_LODS);
					Assert(ePart < partNUMPARTS); 
					m_pModels[nLod][ePart] = pModel;
				}

				T.GetToken(nToken, 256);
			}
		}

		T.GetToken(nToken, 256);
	}

	ASSET.Close(S);
}



const	rmcModel	*mcCityModelInfo::GetModel	(const unsigned int nLod, const mcBuildingParts ePart)
{
	Assert(nLod < NUM_CITY_LODS);

	if	(m_pModels[nLod][ePart])
		return(m_pModels[nLod][ePart]);
	else
		return(m_pModels[nLod ^ 1][ePart]);
}





