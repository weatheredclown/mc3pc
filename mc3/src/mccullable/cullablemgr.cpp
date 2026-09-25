
#include "core/output.h"
#include "vector/matrix34.h"
#include "vector/matrix44.h"
#include "vector/vector4.h"
#include "parse/fileio.h"
#include "ptx_base/draw.h"
#include "mccullable/cullableclass.h"
#include "gfx/ptsprite.h"
class Matrix34;

mcCullableMgr	*mcCullableMgr::s_pCullableMgr = NULL;
u16				mcCullableMgr::s_nDrawPasses = 0x7FF;

bool			mcCullableMgr::s_Wireframe = false;

mcCullableMgr::mcCullableMgr	(void)
{
	s_pCullableMgr = this;
	m_nCullableClassCount = 0;

	for	(int nI = 0; nI < CULLABLE_CLASS_ORDER_MAX; nI++)
		m_pCullableClasses[nI] = NULL;

}

mcCullableMgr::~mcCullableMgr	(void)
{

	s_pCullableMgr = NULL;
}

void	mcCullableMgr::AddClass	(mcCullableClassOrder	eOrder, mcCullableClass *pClass)
{
	Assert((eOrder >= 0) && (eOrder < CULLABLE_CLASS_ORDER_MAX));

	if	(!s_pCullableMgr)
		new	mcCullableMgr;

	Assert(!s_pCullableMgr->m_pCullableClasses[eOrder] && "mcCullableMgr already has a CullableClass at that order position");

	s_pCullableMgr->m_pCullableClasses[eOrder] = pClass;
	s_pCullableMgr->m_nCullableClassCount++;

}

void	mcCullableMgr::RemoveClass	(mcCullableClass *pClass)
{
	for	(int nI = 0; nI < CULLABLE_CLASS_ORDER_MAX; nI++)
	{
		if	(s_pCullableMgr->m_pCullableClasses[nI] == pClass)
		{
			s_pCullableMgr->m_pCullableClasses[nI] = NULL;

			if	(!--s_pCullableMgr->m_nCullableClassCount)
				delete s_pCullableMgr;

			return;
		}
	}

	Assert(0 && "mcCullableMgr did not find the requested CullableClass to remove");
}

// -gputime row names, one per dtXXX bit in mcPassTypes order.  Static literals:
// the profiler keys rows by the pointer it is handed.
static const char *const s_PassNames[] =
{
	"pass:Culling", "pass:ReflectingGround", "pass:ReflectedObjects", "pass:MainShadowed",
	"pass:Shadows", "pass:MainUnshadowed", "pass:PlayerVehicle", "pass:LightGlows",
	"pass:PtxSystems", "pass:HeadlightCone", "pass:Alpha"
};
