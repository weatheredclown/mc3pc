#ifndef	MCLEVEL_CITYMODEL_H
#define	MCLEVEL_CITYMODEL_H

#include	"mccullable/cullableclass.h"
#include	"mccullable/cullable.h"
#include	"mccullable/cullabletype.h"

#include	"mclevel/citymodelinfo.h"

class	mcCityModelClass;
class	mcCityModelType;

typedef	CullableClass	< mcCityModelClass, mcCullableClass, (dtCULLING | dtREFLECTED_OBJECTS | dtREFLECTING_GROUND | dtMAIN_SHADOWED | dtALPHA), CITY_MODELS_PRIORITY >	CityModelClassBase;

class	mcCityModelClass	:	public	CityModelClassBase
{
	private:

		int						m_nNumPartMain;
		int						m_nNumPartGround;
		int						m_nNumPartReflect;
		int						m_nNumPartHDR;
		int						m_nNumPartAlpha;

	public:
								mcCityModelClass	(void);
								mcCityModelClass	(class datResource &rsc);
		void					SetRenderStates		(const mcPassTypes ePassMask) const;
		void					RestoreRenderStates	(const mcPassTypes ePassMask) const;
		void					RenderAllTypes		(mcPassTypes ePassMask);

	friend	class	mcCityModelType;
};

class	mcCityModel	:	public	mcCullable
{
	private:
		u8						m_nCellMinX;
		u8						m_nCellMaxX;
		u8						m_nCellMinZ;
		u8						m_nCellMaxZ;

	protected:
		float					m_fRadius;
		int						m_nGroup;

	public:
								mcCityModel			(void);
								mcCityModel			(class datResource &rsc);
		virtual					~mcCityModel		(void);
		virtual	bool			LoadModels			(const char *pName);
		virtual void			Render				(mcPassTypes ePassMask);
		virtual void			AddAreaLight		(void);
		void					SetGroup			(int nGroup) { m_nGroup = nGroup; }
		int						GetGroup			(void) const { return(m_nGroup); }
		void					SetRadius			(float fRadius) { m_fRadius = fRadius; }
		void					SetCellBounds		(u8 minX, u8 maxX, u8 minZ, u8 maxZ) { m_nCellMinX = minX; m_nCellMaxX = maxX; m_nCellMinZ = minZ; m_nCellMaxZ = maxZ; }
		virtual void			InitExtents			(const Vector3 &emin, const Vector3 &emax);

		mcCityModelType			*GetCityModelType	(void) const { return((mcCityModelType *)GetType()); }

		virtual	float			GetRadius			(void) const { return(m_fRadius); }
};

class	mcCityModelType	:	public	mcCullableType
{
	private:
		mcCityModelInfo			m_ModelInfo;

	public:
								mcCityModelType		(void);
								mcCityModelType		(class datResource &rsc);
		virtual					~mcCityModelType	(void);
		void					LoadTypeData		(const char *pName, const char *pExt);
		void					RenderAllInstances	(mcPassTypes ePassMask);
		void					Render				(mcCityModel &rCityModel, mcPassTypes ePassMask, int nCpvIndex);

		const	Vector3			&GetCenter			(void) const { return(m_ModelInfo.GetCenter()); }
		const	float			GetRadius			(void) const { return(m_ModelInfo.GetRadius()); }
		const	rmcModel		*GetModel			(const int nLod, const mcBuildingParts ePart) { return(m_ModelInfo.GetModel(nLod, ePart)); }
		mcCityModelInfo			&GetModelInfo		(void) { return(m_ModelInfo); }

};

#endif
