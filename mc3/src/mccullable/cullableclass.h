#ifndef	MCCULLABLE_CULLABLECLASS_H
#define	MCCULLABLE_CULLABLECLASS_H

#include	"mccullable/cullablemgr.h"
#include	"mccullable/cullabledef.h"

class	mcCullableType;

class	bkGroup;

extern	void	CullableTypeFixup	(mcCullableClassOrder eOrder, void *pType, class datResource &rsc);

class	mcCullableClass
{
	private:
		int					m_nPassTypes;
		mcCullableType		*m_pTypes;
		mcCullableType		*m_pFirstActiveType;
		bool				m_bDrawEnable;

	protected:
		bkGroup				*m_pGroup;
		bool				m_bDrawBoundingSpheres;
		bool				m_bDrawLabels;

	public:
							mcCullableClass		(void);
							mcCullableClass		(class datResource &rsc);
		virtual				~mcCullableClass	(void);
		template < class T > mcCullableType	*LoadType	(T *, const char *pName, const char *pExt);
		mcCullableType		*FindType			(const char *pName) const;
		void				AddType				(mcCullableType *pType);
		void				RemoveType			(mcCullableType *pType);
		void				SetFirstActiveType	(mcCullableType *pType) { m_pFirstActiveType = pType; }
		mcCullableType		*GetFirstActiveType	(void) const { return(m_pFirstActiveType); }
		virtual	void		SetRenderStates		(const mcPassTypes ePassMask) const = 0;
		virtual	void		RestoreRenderStates	(const mcPassTypes ePassMask) const = 0;
		virtual	void		RenderAllTypes		(mcPassTypes ePassMask);
		void				SetRenderPassTypes	(int nPassTypes) { m_nPassTypes = nPassTypes; }

		bool				IsEnabled			(void) const { return(m_bDrawEnable); }

};

template < class T > mcCullableType	*mcCullableClass::LoadType	(T *, const char *pName, const char *pExt)
{
	mcCullableType	*pType;

	pType = FindType(pName);

	if	(pType)
	{
		pType->AddRef();
		return(pType);
	}

	pType = new T;

	pType->SetName(pName);

	AddType(pType);

	pType->LoadTypeData(pName, pExt);

	pType->Reset();

	return(pType);
}

template < class T, class BaseClass, int Passes, mcCullableClassOrder eDrawOrder > class CullableClass : public BaseClass
{
	private:
		static	T				*m_pInstance;
		int						m_nRefCount;

	protected:
		mcCullableClassOrder	m_eDrawOrder;

	public:
		CullableClass	(void)
		{
			m_nRefCount = 1;
			SetRenderPassTypes(Passes);
			m_eDrawOrder = eDrawOrder;
		}

		CullableClass	(class datResource &rsc)	:	BaseClass(rsc)
		{
			m_pInstance = (T *)this;
			CullableTypeFixup(m_eDrawOrder, GetFirstType(), rsc);
		}

		~CullableClass	(void)
		{
			mcCullableMgr::RemoveClass(this);
			m_pInstance = NULL;
		}

		static	T	*CreateInstance	(void)
		{
			if	(!m_pInstance)
			{
				m_pInstance = new T;
				return(m_pInstance);
			}
			else
			{
				m_pInstance->m_nRefCount++;
				return(m_pInstance);
			}
		}

		static	T		*GetInstance	(void) { Assert(m_pInstance); return(m_pInstance); }
		static	void	Release			(void) {
			if (m_pInstance) {
				if (!--m_pInstance->m_nRefCount) {
					delete m_pInstance;
					m_pInstance = NULL;
				}
			}
		}
		const	int		GetRefCount		(void) const { return(m_nRefCount); }
};

template < class T, class BaseClass, int Passes, mcCullableClassOrder eOrder> T *CullableClass < T, BaseClass, Passes, eOrder >::m_pInstance = NULL;

#endif
