////////////////////////////////////////
// skeleton.h
////////////////////////////////////////

#ifndef CRSKELETON_SKELETON_H
#define CRSKELETON_SKELETON_H

#include "core/types.h"
#include "vector/matrix44.h"
#include "crskeleton/bone.h"
#include "crskeleton/skeldata.h"

////////////////////////////////////////////////////////////////////////////
//
// crSkeleton — a live instance of a skeleton: the runtime crBone array bound to
// a shared crSkeletonData.  Forward kinematics resolves each bone's global
// transform from its local transform + parent (Update()); consumers read the
// resolved matrices for skinning / attachment.
//
////////////////////////////////////////////////////////////////////////////

class crSkeleton
{
public:
	enum { MAX_BONES = 128 };

	crSkeleton(int numBones = 0, bool simple = false) : SkelData(NULL), Bones(NULL), ParentMtx(&Matrix34::I), OwnsBones(false) {}
	// Resource-image skeleton: fixes up the data/bone pointers in place.
	crSkeleton(class datResource &rsc);
	~crSkeleton();

	//// Lifecycle ////
	// Bind to shared static data + allocate the runtime bones (optionally under a
	// parent transform).  Update() resolves global transforms via forward
	// kinematics.  Attach()/AttachModelRelative() write the resolved bone matrices
	// into a caller matrix array (e.g. MTX) for skinning.
	void Init(crSkeletonData &sd,Matrix34 *parentMtx=0);
	void Update();
	void SetParentMtx(const Matrix34 *m) {ParentMtx = m ? m : &Matrix34::I;}
	// Write the resolved bone transforms into a Matrix44 palette (the skinning
	// matrices consumed by gfxModel::Draw).
	void Attach(Matrix44 *mtx) const;
	void AttachModelRelative(Matrix44 *mtx) const;
	void Attach(Matrix34 *mtx) const;
	void AttachModelRelative(Matrix34 *mtx) const;

	//// Data ////
	bool HasSkeletonData() const					{return SkelData != NULL;}
	crSkeletonData *GetSkeletonDataPtr() const		{return SkelData;}
	crSkeletonData &GetSkeletonData()				{return *SkelData;}
	const crSkeletonData &GetSkeletonData() const	{return *SkelData;}

	//// Bones ////
	crBone *GetBones() const						{return Bones;}
	crBone &GetBone(int i)							{return Bones[i];}
	const crBone &GetBone(int i) const				{return Bones[i];}

	crBone &GetBone(const crBoneData &bd)			{return Bones[SkelData->IndexOf(bd)];}
	const crBone &GetBone(const crBoneData &bd) const{return Bones[SkelData->IndexOf(bd)];}

	Matrix34 &GetGlobalMtx(int i)					{return Bones[i].GetGlobalMtx();}
	const Matrix34 &GetGlobalMtx(int i) const		{return Bones[i].GetGlobalMtx();}

	Matrix34 &GetGlobalMtx(const crBoneData *bd)				{return GetGlobalMtx(SkelData->IndexOf(*bd));}
	const Matrix34 &GetGlobalMtx(const crBoneData *bd) const	{return GetGlobalMtx(SkelData->IndexOf(*bd));}

	Matrix34 &GetLocalMtx(int i)					{return Bones[i].GetLocalMtx();}
	const Matrix34 &GetLocalMtx(int i) const		{return Bones[i].GetLocalMtx();}

	Matrix34 &GetLocalMtx(const crBoneData *bd)				{return GetLocalMtx(SkelData->IndexOf(*bd));}
	const Matrix34 &GetLocalMtx(const crBoneData *bd) const	{return GetLocalMtx(SkelData->IndexOf(*bd));}

	//// Hierarchy / placement ////
	const Matrix34 *GetParentMtx() const			{return ParentMtx;}
	int GetParentIndex(int i) const					{return SkelData->GetBone(i)->Parent;}
	const Vector3 &GetOffset(int i) const			{return Bones[i].GetOffset();}

	crSkeletonData *SkelData;	// +00  shared static data
	crBone *Bones;				// +04  per-instance runtime bones
	const Matrix34 *ParentMtx;	// transform this skeleton hangs under -- a live
								// pointer (never NULL): the owner keeps moving the
								// actor matrix after Init, and FK must track it
	Vector3 Offset;				// root offset
	bool OwnsBones;
};

#endif // CRSKELETON_SKELETON_H
