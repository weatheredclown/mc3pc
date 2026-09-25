#include	"data/string.h"

#include	"mccullable/cullabletype.h"
#include	"mccullable/cullable.h"
#include	"mccullable/cullableclass.h"

mcCullableType::mcCullableType	(void)
{
	m_nRefCount = 1;
	m_pClass = NULL;
	m_pName = NULL;
	m_pNextType = NULL;
	m_pFirstActiveCullable = NULL;
	m_pPrevActiveType = NULL;
	m_pNextActiveType = NULL;
}

mcCullableType::~mcCullableType	(void)
{
	MakeInActive();

	if (m_pClass)
		m_pClass->RemoveType(this);

	if	(m_pName)
		delete	(char *)m_pName;
}

void	mcCullableType::SetName	(const char *pName)
{
	Assert(!m_pName);

	m_pName = StringDuplicate(pName);
}

void	mcCullableType::AddToActive	(mcCullable *pInstance)
{
	pInstance->SetPrev(NULL);
	pInstance->SetNext(m_pFirstActiveCullable);

	if	(m_pFirstActiveCullable)
	{
		m_pFirstActiveCullable->SetPrev(pInstance);
	}
	else
	{
		MakeActive();
	}

	m_pFirstActiveCullable = pInstance;
}

void	mcCullableType::RemoveFromActive	(mcCullable *pInstance)
{
	if	(pInstance->GetPrev())
	{
		pInstance->GetPrev()->SetNext(pInstance->GetNext());
	}
	else
	{
		m_pFirstActiveCullable = pInstance->GetNext();
	}

	if	(pInstance->GetNext())
	{
		pInstance->GetNext()->SetPrev(pInstance->GetPrev());
	}

	if	((pInstance->GetNext() == NULL) && (pInstance->GetPrev() == NULL))
	{
		if	(m_pFirstActiveCullable)
			return;

		MakeInActive();
	}
	else
	{
		pInstance->SetPrev(NULL);
		pInstance->SetNext(NULL);
	}
}

void	mcCullableType::Reset	(void)
{
	mcCullable	*pCullable = m_pFirstActiveCullable;
	m_pFirstActiveCullable = NULL;

	while	(pCullable)
	{
		pCullable->Enable(false);
		pCullable = pCullable->GetNext();
	}

	m_pPrevActiveType = NULL;
	m_pNextActiveType = NULL;
}

void	mcCullableType::MakeActive	(void)
{
	if (!m_pClass)
		return;

	if	(m_pPrevActiveType || m_pNextActiveType || (m_pClass->GetFirstActiveType() == this))
		return;

	m_pPrevActiveType = 0;
	m_pNextActiveType = m_pClass->GetFirstActiveType();

	if	(m_pClass->GetFirstActiveType())
	{
		m_pClass->GetFirstActiveType()->m_pPrevActiveType = this;
	}

	m_pClass->SetFirstActiveType(this);
}

void	mcCullableType::RenderAllInstances	(mcPassTypes ePassMask)
{
	mcCullable	*pInstance = m_pFirstActiveCullable;

	while	(pInstance)
	{
		pInstance->Render(ePassMask);
		pInstance = pInstance->GetNext();
	}
}

void	mcCullableType::MakeInActive	(void)
{
	if (!m_pClass)
	{
		m_pPrevActiveType = m_pNextActiveType = NULL;
		return;
	}

	if	(!m_pPrevActiveType && !m_pNextActiveType && (m_pClass->GetFirstActiveType() != this))
		return;

	if	(m_pPrevActiveType)
	{
		m_pPrevActiveType->m_pNextActiveType = m_pNextActiveType;
	}
	else
	{
		m_pClass->SetFirstActiveType(m_pNextActiveType);
	}

	if	(m_pNextActiveType)
	{
		m_pNextActiveType->m_pPrevActiveType = m_pPrevActiveType;
	}

	m_pPrevActiveType = m_pNextActiveType = NULL;
}

#include	"data/resourcehelpers.h"

mcCullableType::mcCullableType(class datResource &rsc)
{
	m_nRefCount = rsc.GetInt();
	rsc.PointerFixup(m_pClass);
	m_pClass = NULL;
	const char *name = rsc.GetStringPtr();
	if (name && *name)
		m_pName = StringDuplicate(name);
	else
		m_pName = NULL;
	rsc.PointerFixup(m_pNextType);
	rsc.PointerFixup(m_pPrevActiveType);
	rsc.PointerFixup(m_pNextActiveType);
	rsc.PointerFixup(m_pFirstActiveCullable);
	rsc.GetVector3(vDummy);
}
