#ifndef	MCCULLABLE_CULLABLETYPE_H
#define	MCCULLABLE_CULLABLETYPE_H

#include	"mccullable/cullabledef.h"

class	mcCullableClass;
class	mcCullable;

class	mcCullableType
{
	private:
		int				m_nRefCount;
		mcCullableClass	*m_pClass;
		const	char	*m_pName;
		mcCullableType	*m_pNextType;
		mcCullableType	*m_pPrevActiveType;
		mcCullableType	*m_pNextActiveType;

	protected:
		mcCullable		*m_pFirstActiveCullable;

	public:
						mcCullableType		(void);
						mcCullableType		(class datResource &rsc);
		virtual			~mcCullableType		(void);

		void			AddRef				(void) { m_nRefCount++; }
		void			Release				(void) { if	(!--m_nRefCount) { delete this; } }

		void			SetClass			(mcCullableClass *pClass) { m_pClass = pClass; }

		void			SetName				(const char *pName);
		const	char	*GetName			(void) const { return(m_pName); }

		void			SetNextType			(mcCullableType *pType) { m_pNextType = pType; }
		mcCullableType	*GetNextType		(void) const { return(m_pNextType); }

		mcCullable		*GetFirstActive		(void) const { return(m_pFirstActiveCullable); }
		mcCullableType	*GetNextActiveType	(void) const { return(m_pNextActiveType); }

		void			AddToActive			(mcCullable *pInstance);
		void			RemoveFromActive	(mcCullable *pInstance);

		virtual	void	LoadTypeData		(const char *pName, const char *pExt) = 0;
		virtual void	RenderAllInstances	(mcPassTypes ePassMask);
		void			Reset				(void);

		virtual float	GetLODDist			(void) const { return(100.0f); }

		Vector3			vDummy;
		virtual	const	Vector3	&GetCenter	(void) const { Assert(0); return(vDummy); }
		virtual	const	float	GetRadius	(void) const { Assert(0); return(0.0f); }

	private:
		void			MakeActive			(void);
		void			MakeInActive		(void);
};

#endif
