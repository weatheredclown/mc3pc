#ifndef MCOCCLUDER_OCCLUDER_H
#define MCOCCLUDER_OCCLUDER_H


class	datAsciiTokenizer;

namespace mcOccluder
{
	const int		FILE_VERSION				= 2;
	const int		MAX_OCCLUDER_POLYEDGES		= 4;
	const int		MAX_VIEWS					= 8;
	const int		MAX_ACTIVE_OCCLUDERS		= 8;
	const float		DEFAULT_ACTIVE_DIST			= 40.f;
};

class mcOccluderPoly
{
	public:
							mcOccluderPoly	(void);
							~mcOccluderPoly	(void);

		const Vector4 &		GetPlane		(void){ return (m_plane);}

	private:
		
		Vector4				m_plane;
		int					m_num_verts;
		Vector3	*			m_pVerts;

		float				m_activeDist;
};

class mcActiveOccluderPoly
{
public:
	mcOccluderPoly*		m_pOccPoly;
	int					m_num_planes;
	Vector4				m_occlusionPlanes[ mcOccluder::MAX_OCCLUDER_POLYEDGES ];

};

class PvsViewer
{
	public:
						PvsViewer				(void);


	private:
		Matrix34		m_CamMtx;

		bool			m_bIsActive;
		Vector3			m_position;
		Vector3			m_viewDirection;
};

class mcOccluderSystem
{
	public:

		static mcOccluderSystem *GetInstance() {return sm_instance;}

						mcOccluderSystem	(void);

		void			EndFrame			(void);

		bool			AreOccludersEnabled (void){ return( m_bOccludersEnabled); }

		int				GetActiveViewID		(void){ return(m_active_viewerID); }

		int				GetVersion			(void){ return(m_version); }

		bool			IsSphereOccluded( const Vector3 &center, const float radius );

	private:

		int				m_OccludedCount;
		int				m_active_viewerID;
		bool			m_bFrameInProgress;
		bool			m_bOccludersEnabled;
		bool			m_bDrawOccludedVolumes;

		PvsViewer		m_views[ mcOccluder::MAX_VIEWS ];

		int					 m_numActiveOccluders[ mcOccluder::MAX_VIEWS ];
		mcActiveOccluderPoly m_activeOccluders[ mcOccluder::MAX_VIEWS ][ mcOccluder::MAX_ACTIVE_OCCLUDERS ];

		bool			Load				( datAsciiTokenizer & );

		int				m_version;
		int				m_numOccluderPolys;
		mcOccluderPoly	*m_OccluderPolyArray;

		char			ErrorString		[128];

		static mcOccluderSystem	*sm_instance;
};

#endif