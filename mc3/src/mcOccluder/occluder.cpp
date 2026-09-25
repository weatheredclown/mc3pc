
#include	"gfx/vglext.h"

#include	"rmcore/state.h"

#include	"mcOccluder/occluder.h"

mcOccluderSystem *mcOccluderSystem::sm_instance = NULL;

extern	bool g_bDrawOccludedVolumes;

static	float	solid_groundplane	= -40.f;

typedef struct OccludedBoundInfo {

	Vector3	center;
	float	radius;

} OccludedBoundInfo;

int	    g_OccludedDrawBoundTotal = 0;

bool	mcOccluderSystem::IsSphereOccluded( const Vector3 &center, const float radius )
{
	if( ! AreOccludersEnabled() )
		return false;

	s32 total_active = m_numActiveOccluders[ GetActiveViewID() ];

	if( !total_active )
		return false;

	int viewID = GetActiveViewID();

	for( int idx=0; idx < total_active; idx++ )
	{
		bool bFail;

		if( m_activeOccluders[ viewID ][ idx ].m_pOccPoly->GetPlane().DistanceToPlane( center ) > -radius )
		{
			continue;
		}

		int numplanes = m_activeOccluders[ viewID ][ idx ].m_num_planes;

		bFail = false;

		for( int plane=0; plane < numplanes; plane++)
		{
			
			if( m_activeOccluders[ viewID ][ idx ].m_occlusionPlanes[ plane ].DistanceToPlane( center ) > -radius )
			{
				bFail = true;
				break;
			}			
		}

		if( !bFail )
		{
			m_OccludedCount++;

			if( g_bDrawOccludedVolumes )
			{
						bool oldZWrite = RSTATE.GetZWriteEnable();
						bool oldZtest = RSTATE.GetZTestEnable();
						bool oldLightingEn = RSTATE.GetLighting();
						bool oldAlphaBlendEnable = RSTATE.GetAlphaBlendEnable();

						RSTATE.SetLighting( false );
						RSTATE.SetZTestEnable( false );
						RSTATE.SetZWriteEnable( true );
						RSTATE.SetAlphaBlendEnable( false );

						vglColor4f( 0.0f, 1.0f, 1.0f, 1.f );
						vglDrawSphere( radius, center , 5, true );
						
						RSTATE.SetZWriteEnable(oldZWrite);
						RSTATE.SetZTestEnable( oldZtest );
						RSTATE.SetLighting( oldLightingEn );
						RSTATE.SetAlphaBlendEnable( oldAlphaBlendEnable );
			}

			return true;
		}
	}

	return false;

}
