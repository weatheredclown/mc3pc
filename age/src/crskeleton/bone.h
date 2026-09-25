////////////////////////////////////////
// bone.h
////////////////////////////////////////

#ifndef CRSKELETON_BONE_H
#define CRSKELETON_BONE_H

#include "vector/vector3.h"
#include "vector/Matrix34.h"

////////////////////////////////////////////////////////////////////////////
//
// crBone — the RUNTIME state of one bone: its current euler angles, its local
// transform (relative to parent) and its resolved global transform (after
// forward kinematics up the hierarchy).
//
////////////////////////////////////////////////////////////////////////////

class crBone
{
public:
	//// Euler ////
	Vector3 &GetEuler()						{return Euler;}
	const Vector3 &GetEuler() const			{return Euler;}

	//// Local transform (bone-relative) ////
	Matrix34 &GetLocalMtx()					{return LocalMtx;}
	const Matrix34 &GetLocalMtx() const		{return LocalMtx;}

	//// Global transform (world/model space) ////
	Matrix34 &GetGlobalMtx()				{return GlobalMtx;}
	const Matrix34 &GetGlobalMtx() const	{return GlobalMtx;}

	// Alias used widely in rb/src for the resolved (global) transform.
	Matrix34 &GetMatrix()					{return GlobalMtx;}
	const Matrix34 &GetMatrix() const		{return GlobalMtx;}

	u32 GetDOFs() const						{return Dofs;}
	const Vector3 &GetOffset() const		{return Offset;}

	Vector3 Euler;			// +00
	Matrix34 LocalMtx;		// +12
	Matrix34 GlobalMtx;		// +60
	u32 Dofs;				// dof mask (mirrors this bone's crBoneData)
	Vector3 Offset;			// current translation offset
};

#endif // CRSKELETON_BONE_H
