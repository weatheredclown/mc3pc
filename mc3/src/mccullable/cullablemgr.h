#ifndef	MCCULLABLE_CULLABLEMGR_H
#define	MCCULLABLE_CULLABLEMGR_H



#include "core/types.h"



#include	"mccullable/cullabledef.h"



class	mcCullableClass;






class	mcCullableMgr
{
	private:
		static	mcCullableMgr	*s_pCullableMgr;
		int						m_nCullableClassCount;
		mcCullableClass			*m_pCullableClasses[CULLABLE_CLASS_ORDER_MAX];
		static	u16				s_nDrawPasses;
		static	bool			s_Wireframe;


	public:
								mcCullableMgr			(void);
								~mcCullableMgr			(void);
		static	void			AddClass				(mcCullableClassOrder eOrder, mcCullableClass *pClass);
		static	void			RemoveClass				(mcCullableClass *pClass);
		static	void			Render					(void);




	private:
};



#endif
