////////////////////////////////////////
// skeleton.cpp
//
// crSkeleton lifecycle (Init / Update / Attach).  Update() resolves each bone's
// global transform from its local transform and parent, rooting the chain at
// *ParentMtx -- the live actor matrix the owner keeps moving after Init.
// crSkeletonData::Load lives in skeldata.cpp (matches skeldata.h).
////////////////////////////////////////

#include "crskeleton/skeleton.h"
#include "atl/array.h"

void crSkeleton::Init(crSkeletonData &sd, Matrix34 *parentMtx)
{
	if (Bones && OwnsBones)
	{
		delete[] Bones;
		Bones = NULL;
	}
	if (SkelData)
		SkelData->Release();
	SkelData = &sd;
	SkelData->AddRef();
	ParentMtx = parentMtx ? parentMtx : &Matrix34::I;
	Offset.Zero();
	int n = sd.GetNumBones();
	if (n < 1) n = 1;
	Bones = new crBone[n];
	OwnsBones = true;
	for (int i = 0; i < n; i++)
	{
		// Rest pose = the bone's rest rotation (XYZ eulers, zero for text
		// skeletons) at its parent-relative offset, as the console Init did.
		Bones[i].Euler = sd.GetBone(i)->GetRotation();
		Bones[i].LocalMtx.Identity();
		Bones[i].LocalMtx.FromEulersXYZ(sd.GetBone(i)->GetRotation());
		Bones[i].LocalMtx.d = sd.GetBone(i)->GetOffset();
		Bones[i].GlobalMtx.Identity();
		Bones[i].Dofs   = sd.GetBone(i)->GetDOFs();
		Bones[i].Offset = sd.GetBone(i)->GetOffset();
	}
}

void crSkeleton::Update()
{
	if (!SkelData) return;

	// Plain hierarchy composition — no facing flip, root rotation applied.
	// Hard-won notes (see age-facing-and-stick memory):
	// * The anims, skeleton, AND bone-local mesh data all natively face -z
	//   (= -c, AGE's creature forward). No flip is needed anywhere. The
	//   session-long facing flip-flopping was animAnimatorComponentType's
	//   UNINITIALIZED Flip180 flag randomly enabling HandleUpdatePass2's
	//   RotateY(PI) on the root — different heap garbage per build.
	// * The root bone's rotation channels MUST be applied: kno's stand anim
	//   roots at (pitch 8.85, yaw -12) and the children are authored to
	//   compensate; with root rotation the stance composes plumb (~3deg),
	//   without it the legs trail ~12deg (idle forward lean).
	for (int i = 0; i < SkelData->GetNumBones(); i++)
	{
		int p = SkelData->GetBone(i)->Parent;
		if (p >= 0)
		{
			Bones[i].GlobalMtx.Dot(Bones[i].LocalMtx, Bones[p].GlobalMtx);
		}
		else
		{
			Bones[i].GlobalMtx.Dot(Bones[i].LocalMtx, *ParentMtx);
		}
	}
}

void crSkeleton::Attach(Matrix44 *mtx) const
{
	if (!SkelData) return;
	const_cast<crSkeleton*>(this)->Update();
	for (int i = 0; i < SkelData->GetNumBones(); i++)
	{
		mtx[i].FromMatrix34(Bones[i].GlobalMtx);
	}
}

void crSkeleton::AttachModelRelative(Matrix44 *mtx) const
{
	if (!SkelData) return;
	const_cast<crSkeleton*>(this)->Update();
	int numBones = SkelData->GetNumBones();
	if (numBones <= 0) return;

	atArray<Matrix34> restGlobals;
	restGlobals.Resize(numBones);
	for (int i = 0; i < numBones; i++)
	{
		Matrix34 localRest;
		localRest.Identity();
		localRest.FromEulersXYZ(SkelData->GetBone(i)->GetRotation());
		localRest.d = SkelData->GetBone(i)->GetOffset();
		int p = SkelData->GetBone(i)->Parent;
		if (p >= 0)
		{
			restGlobals[i].Dot(localRest, restGlobals[p]);
		}
		else
		{
			restGlobals[i] = localRest;
		}
	}

	for (int i = 0; i < numBones; i++)
	{
		Matrix34 restInv;
		restInv.FastInverse(restGlobals[i]);

		Matrix34 animMtx;
		animMtx.Dot(restInv, Bones[i].GlobalMtx);
		mtx[i].FromMatrix34(animMtx);
	}
}

void crSkeleton::Attach(Matrix34 *mtx) const
{
	if (!SkelData) return;
	const_cast<crSkeleton*>(this)->Update();
	for (int i = 0; i < SkelData->GetNumBones(); i++)
	{
		mtx[i] = Bones[i].GlobalMtx;
	}
}

void crSkeleton::AttachModelRelative(Matrix34 *mtx) const
{
	if (!SkelData) return;
	const_cast<crSkeleton*>(this)->Update();
	int numBones = SkelData->GetNumBones();
	if (numBones <= 0) return;

	atArray<Matrix34> restGlobals;
	restGlobals.Resize(numBones);

	for (int i = 0; i < numBones; i++)
	{
		Matrix34 localRest;
		localRest.Identity();
		localRest.FromEulersXYZ(SkelData->GetBone(i)->GetRotation());
		localRest.d = SkelData->GetBone(i)->GetOffset();
		int p = SkelData->GetBone(i)->Parent;
		if (p >= 0)
		{
			restGlobals[i].Dot(localRest, restGlobals[p]);
		}
		else
		{
			restGlobals[i] = localRest;
		}
	}

	for (int i = 0; i < numBones; i++)
	{
		Matrix34 restInv;
		restInv.FastInverse(restGlobals[i]);
		mtx[i].Dot(restInv, Bones[i].GlobalMtx);
	}
}

#include "data/resource.h"

crSkeleton::crSkeleton(datResource &rsc)
{
#if defined(__WIN32PC)
	// The image holds two pointers: the shared crSkeletonData and this
	// instance's crBone array.  On the console both were live memory and
	// relocating them was the whole job.  Here the data has to be built -
	// crSkeletonData's own resource constructor is what turns the packed bones
	// into a hierarchy with parent indices and DOF counts - and the bone array
	// is pure per-instance runtime state that the pack only carries along, so
	// it is dropped and Init allocates a live one from the rest pose.
	//
	// Without this a resourced skeleton (a city pedestrian: see
	// mcCreatureType's resource constructor) came out holding tagged image
	// addresses, and the first GetNumBones() on it read from unmapped memory.
	SkelData = NULL;
	Bones = NULL;
	ParentMtx = &Matrix34::I;
	OwnsBones = false;

	crSkeletonData *data = NULL;
	rsc.PointerFixup(data);
	rsc.Construct(data);
	void *instanceBones = NULL;
	rsc.PointerFixup(instanceBones);
	if (data)
		Init(*data, NULL);
#else
	rsc.PointerFixup(SkelData);
	rsc.PointerFixup(Bones);
	ParentMtx = &Matrix34::I;
	OwnsBones = false;
#endif
}

crSkeleton::~crSkeleton()
{
	if (Bones)
	{
		if (OwnsBones)
			delete[] Bones;
		Bones = NULL;
	}
	if (SkelData)
	{
		if (OwnsBones)
			SkelData->Release();
		SkelData = NULL;
	}
}
