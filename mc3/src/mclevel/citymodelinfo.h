#ifndef	MCLEVEL_CITYMODELINFO_H
#define	MCLEVEL_CITYMODELINFO_H



#include	"vector/vector4.h"



#define	NUM_CITY_LODS		2



enum	mcBuildingParts
{ 
	partMAIN,
	partGROUND,
	partREFLECT,
	partHDR,
	partALPHA,
	partNUMPARTS
};



class	rmcModel;



class	mcCityModelInfo
{
	private:
		Vector3				m_vCenter;
		float				m_fRadius;
		rmcModel			*m_pModels[NUM_CITY_LODS][partNUMPARTS];

	public:
							mcCityModelInfo		(void);
							mcCityModelInfo		(class datResource &rsc);
							~mcCityModelInfo	(void);
		void				Load				(const char *pName, const char *pExt);
		const	Vector3		&GetCenter			(void) const { return(m_vCenter); }
		float				GetRadius			(void) const { return(m_fRadius); }
		void				SetCenter			(const Vector3 &c) { m_vCenter = c; }
		void				SetRadius			(float r) { m_fRadius = r; }
		void				SetModel			(unsigned int nLod, mcBuildingParts ePart, rmcModel *pModel) { if (nLod < NUM_CITY_LODS && (unsigned int)ePart < (unsigned int)partNUMPARTS) m_pModels[nLod][ePart] = pModel; }
		const	rmcModel	*GetModel			(const unsigned int nLod, const mcBuildingParts ePart);
};



#endif
