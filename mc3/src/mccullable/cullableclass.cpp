
#include	"mccullable/cullableclass.h"
#include	"mccullable/cullabletype.h"

mcCullableClass::mcCullableClass	(void)
{
	m_pTypes = NULL;
	m_pFirstActiveType = NULL;
	m_bDrawEnable = true;

}

mcCullableClass::~mcCullableClass	(void)
{
}

mcCullableType	*mcCullableClass::FindType	(const char *pName)	const
{
	mcCullableType	*pType = m_pTypes;

	while	(pType)
	{
		if	(strcmp(pType->GetName(), pName) == 0)
			return(pType);

		pType = pType->GetNextType();
	}

	return(pType);
}

void	mcCullableClass::AddType	(mcCullableType *pType)
{
	pType->SetNextType(m_pTypes);
	pType->SetClass(this);
	m_pTypes = pType;
}

void	mcCullableClass::RemoveType	(mcCullableType *pType)
{
	if (m_pTypes == pType)
	{
		m_pTypes = pType->GetNextType();
		pType->SetNextType(NULL);
		pType->SetClass(NULL);
		return;
	}

	mcCullableType	*pPrevType = m_pTypes;

	while (pPrevType && pPrevType->GetNextType())
	{
		if (pPrevType->GetNextType() == pType)
		{
			pPrevType->SetNextType(pType->GetNextType());
			pType->SetNextType(NULL);
			pType->SetClass(NULL);
			return;
		}
		pPrevType = pPrevType->GetNextType();
	}

	Warningf("Failed to Remove Type %s @ %p", pType->GetName(), pType);
}

void	mcCullableClass::RenderAllTypes	(mcPassTypes ePassMask)
{
	mcCullableType	*pType = m_pFirstActiveType;

	while	(pType)
	{
		pType->RenderAllInstances(ePassMask);
		pType = pType->GetNextActiveType();
	}
}
