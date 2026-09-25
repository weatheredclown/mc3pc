
#ifndef __SHDRAW_DRAWABLE_H
#define __SHDRAW_DRAWABLE_H

#include "data/base.h"
#include "rmcore/lodgroup.h"

class gfxEdgeModel;

#pragma warning( disable: 4710 )

class drwDrawable : public Base
{
public:
	drwDrawable() {};
	virtual ~drwDrawable() {};

	enum {
		LOD_H = ::LOD_HIGH,
		LOD_M = ::LOD_MED,
		LOD_L = ::LOD_LOW,
		LOD_VL = ::LOD_VLOW,
		LOD_COUNT = ::LOD_COUNT 
	};

	enum drwType { DRW_MODEL, DRW_BONYMODEL, DRW_WATER, DRW_FX, DRW_CARD };

	virtual void Draw( int lod, const Matrix34 &mtx, int block ) = 0;

	virtual gfxEdgeModel* GetShadow( int lod ) const;

	virtual float GetCullRadius() = 0;
	virtual void SetCullRadius( float radius ) = 0;

	virtual bool IsA( drwType ) = 0;

	virtual void GetBoundingBox( Vector3 *boxMin, Vector3 *boxMax, int   ) { boxMin->Set(0.f); boxMax->Set(0.f);}

	virtual void Reset();

	virtual int CalcLod( float zDist ) = 0;
};

#include "rmcore/drawable.h"
#include "data/hash.h"

class animAnimation;

struct drwBonyData {
	drwBonyData() : m_pTagHash(0), m_pAnimHash(0) {}
	drwBonyData(datResource &);

	HashTable		*m_pTagHash;

	HashTable		*m_pAnimHash;

	atArray<animAnimation *>	m_Animations;
};

class drwShaderModel : public rmcDrawable
{
public:
	drwShaderModel();
	virtual ~drwShaderModel();

	drwShaderModel(datResource &);

	void AddRef();
	void Release();
	static void Delete(drwShaderModel *m) { if (m) m->Release(); }
	void Delete() { Release(); }

	bool PreLoadBonetags(const char *name, rmcTypeFileParser *parser = 0);
	virtual bool Load(const char *basename,rmcTypeFileParser *parser=0,bool configParser=true);
	virtual bool Load(datTokenizer & T,rmcTypeFileParser *parser=0,bool configParser=true);
	virtual void LoadMesh( rmcTypeFileCbData *data, bool useCpv = false);

	virtual void LoadSkel( rmcTypeFileCbData *data );

virtual void LoadBounds( rmcTypeFileCbData *data );

	virtual void Draw(const rmcShaderData *data,const Matrix34 &mtx,int bucket,int lod,atBitSet *enables = 0,int variant=0) const;
	virtual void DrawCpv(const rmcShaderData *data,const Matrix34 &mtx,int bucket,int lod,int cpvIndex,int variant = 0) const;

	void GetBoundingBox( Vector3 *boxMin, Vector3 *boxMax ) {
		rmcDrawable::GetBoundingBox(*boxMin, *boxMax);
		if (boxMin->IsEqual(*boxMax, 1e-4f))
			m_LodGroup.GetBoundingBox(*boxMin, *boxMax);
	}

	void SetAlphaRef( u8 ref );

	rmcShaderData *AllocateLocals();

	drwBonyData *GetBonyData() { return m_BonyData; }

	inline int CalcLod( float zDist ) { return GetLodGroup().ComputeLod(zDist); }

	void				SetBound(phBound* pBound)	{ m_Bound = pBound; }

protected:
	void LoadBoneTags( rmcTypeFileCbData *data );
	void LoadAnimations( rmcTypeFileCbData *data );
	void AddAnim( const char *filename, const char *playname );
	
	phBound		*m_Bound;

	s16			m_RefCount;
	drwBonyData	*m_BonyData;
	static rmcShaderGroup	*s_ParentShaderGroup;
	static int				s_ParentShaderCount;
};

#endif