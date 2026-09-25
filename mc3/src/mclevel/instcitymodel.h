#ifndef	MCCITY_INSTCITYMODEL_H
#define	MCCITY_INSTCITYMODEL_H


#include	"mclevel/citymodel.h"

class	mcInstCityModelClass;

typedef	CullableClass	< mcInstCityModelClass, mcCullableClass, (dtCULLING | dtREFLECTED_OBJECTS | dtREFLECTING_GROUND | dtMAIN_SHADOWED | dtALPHA), INST_CITY_MODELS_PRIORITY >	InstCityModelClassBase;

class	mcInstCityModelClass	:	public	InstCityModelClassBase
{
	private:

		int						m_nNumPartMain;
		int						m_nNumPartGround;
		int						m_nNumPartReflect;
		int						m_nNumPartHDR;
		int						m_nNumPartAlpha;

	public:
								mcInstCityModelClass	(void);
								mcInstCityModelClass	(class datResource &rsc);

	protected:
		void					SetRenderStates			(const mcPassTypes ePassMask) const;
		void					RestoreRenderStates		(const mcPassTypes ePassMask) const;
		void					RenderAllTypes			(mcPassTypes ePassMask);

	public:

	friend	class	mcInstCityModel;
	friend	class	mcInstCityModelType;
};

class	mcInstCityModel	:	public	mcCityModel
{
	protected:
		Vector3					m_CullCenter;
		Matrix34				m_Matrix;
		int						m_nCPVIndex;

	public:
								mcInstCityModel			(void);
								mcInstCityModel			(class	datResource &rsc);
		virtual					~mcInstCityModel		(void);
		virtual	bool			LoadModels				(const char *pType);
		virtual	void			Render					(mcPassTypes ePassMask);
		void					AddAreaLight			(void);
		void					SetRawMatrix			(const Matrix34 &m) { m_Matrix = m; }
		void					SetCullCenter			(const Vector3 &c) { m_CullCenter = c; }
		void					SetCPVIndex				(int CPVIndex) { m_nCPVIndex = CPVIndex; }

		virtual	const	Vector3 &GetCenter				(void) const { return(m_Matrix.d); }
		virtual	const	Vector3 &GetCullCenter			(void) const { return(m_CullCenter); }
		mcInstCityModelType		*GetInstCityModelType	(void) { return((mcInstCityModelType *)GetType()); }
};

class	mcInstCityModelType	:	public	mcCityModelType
{
	public:
								mcInstCityModelType		(void);
								mcInstCityModelType		(class datResource &rsc);
		virtual					~mcInstCityModelType	(void);
		void					Render					(mcInstCityModel &rCityModel, mcPassTypes ePassMask, int nCpvIndex);
};

#endif
