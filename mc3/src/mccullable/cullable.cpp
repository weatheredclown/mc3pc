#include	"gfx/simple.h"
#include	"gfx/vgl.h"

#include	"mccullable/cullable.h"
#include	"mccullable/cullabletype.h"
#include	"core/types.h"
#include	"mcoccluder/occluder.h"

#ifndef _WIN64
CompileTimeAssert(sizeof(mcCullable) == 4 + ((2 * sizeof(mcCullable *)) + (4 * sizeof(u8)) + sizeof(mcCullableType *)));
#endif

mcCullable::mcCullable	(void)
{
	m_pPrev = NULL;
	m_pNext = NULL;
	m_pType = NULL;
	m_bData0 = 0;
	m_bData1 = 0;
	m_bData2 = 0;
	m_bData3 = 0;
}

mcCullable::~mcCullable	(void)
{
}

bool	mcCullable::IsEnabled	(void)
{
	if	(!m_pType)
		return(false);

	if	((m_pPrev == NULL) && (m_pNext == NULL))
	{
		if	(m_pType->GetFirstActive() != this)
		{
			return(false);
		}
	}

	return(true);
}

void	mcCullable::Enable	(bool bOn)
{
	if	(bOn)
	{
		if	(!IsEnabled())
		{
			EnableRendering();
		}
	}
	else
	{
		if	(IsEnabled())
		{
			DisableRendering();
		}
	}
}

bool	mcCullable::SetClippingAndSphereTest	(void)
{
	Vector3	Center = GetCullCenter();
	float	fRadius = GetRadius();
	Vector4	Center2(Center.x, Center.y, Center.z, fRadius);

	if	(PIPE.GetViewport()->FastSphereVisCheck(Center2) != cullOutside)
	{

		// PC port: the game stood the occluder system up as it started; without one there is
		// nothing to be occluded by.
		mcOccluderSystem	*pOccluders = mcOccluderSystem::GetInstance();

		if	(pOccluders && pOccluders->IsSphereOccluded(Center, fRadius))
		{
			return false;
		}

		return(true);
	}
	else
	{
		return(false);
	}
}

const	Vector3	&mcCullable::GetCenter	(void) const
{
	return(m_pType->GetCenter());
}

const	Vector3	&mcCullable::GetCullCenter	(void) const
{
	return(m_pType->GetCenter());
}

float	mcCullable::GetRadius	(void) const
{
	return(m_pType->GetRadius());
}

void	mcCullable::Init	(void)
{
	m_pPrev = NULL;
	m_pNext = NULL;
}

void	mcCullable::CalcLOD	(void)
{
	const	float	lodDist = GetType()->GetLODDist();
	float	dist2 = GetCullCenter().Dist2(RSTATE.GetCameraPosition());
	SetLOD((dist2 < square(lodDist + GetRadius())) ? (s8)0 : (s8)1);
}

void	mcCullable::EnableRendering	(void)
{
	if	(m_pType)
		m_pType->AddToActive(this);
}

void	mcCullable::DisableRendering	(void)
{
	if	(m_pType)
		m_pType->RemoveFromActive(this);
}
