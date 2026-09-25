////////////////////////////////////////
// skeldata.h
////////////////////////////////////////

#ifndef CRSKELETON_SKELDATA_H
#define CRSKELETON_SKELDATA_H

#include "core/types.h"
#include "vector/vector3.h"
#include <string.h>

// Population count for dof masks.
inline int crCountBits(u32 v){ int n=0; while(v){ v&=v-1; n++; } return n; }

////////////////////////////////////////////////////////////////////////////
//
// crBoneData — the STATIC (load-time) description of one bone: its rest offset
// from the parent, its degrees of freedom, and its place in the hierarchy
// (first child + next sibling linked list).
//
// NOTE: field layout is provisional — match it to the .skel on-disk format (see
// the Rust Oni2Rebuilt skeleton parser) when the loader is written.
//
////////////////////////////////////////////////////////////////////////////

class crBoneData
{
public:
	//// Degrees of freedom -- dof-mask flags.  These MUST be single-bit flag
	//// values, not sequential indices: rb animation code (crAnimFrame::Pose /
	//// Mirror, frame.cpp) tests them as raw masks, e.g. `Dofs & ROTATE_X`.  If
	//// they were indices (0,1,2,...) that test would read the wrong bits and the
	//// per-bone channel walk would over-count, tripping the frame.cpp:588
	//// curdata<NumChannels assert. AGE's own getters below therefore also use
	//// them directly (no 1<<).
	enum
	{
		ROTATE_X	= 1 << 0,
		ROTATE_Y	= 1 << 1,
		ROTATE_Z	= 1 << 2,
		TRANSLATE_X	= 1 << 3,
		TRANSLATE_Y	= 1 << 4,
		TRANSLATE_Z	= 1 << 5,
		SCALE_X		= 1 << 6,
		SCALE_Y		= 1 << 7,
		SCALE_Z		= 1 << 8,
		NUM_DOFS	= 9,

		DEFAULT_DOFS		= ROTATE_X | ROTATE_Y | ROTATE_Z,
		USE_LOCKED_ROT		= (1<<NUM_DOFS),
		USE_LOCKED_SCALE	= (1<<(NUM_DOFS+1))
	};

	crBoneData()
		: RestPosition()
		, Name(nullptr)
		, Dofs(0)
		, Next(nullptr)
		, Child(nullptr)
		, ParentPtr(nullptr)
		, Offset()
		, Rotation()
		, Index(0)
		, MirrorIndex(0)
		, MirrorOffset(0)
		, Flags(0)
		, Pad(0)
		, Mirror(nullptr)
		, Parent(-1)
	{}
	crBoneData(class datResource &rsc);

	//// Access ////
	const Vector3 &GetOffset() const		{return Offset;}
	const Vector3 &GetDefaultTranslation() const { return Offset; }
	Vector3 GetTransMin() const				{return Vector3(0.0f, 0.0f, 0.0f);}
	Vector3 GetTransMax() const				{return Vector3(0.0f, 0.0f, 0.0f);}
	const crBoneData *GetChild() const		{return Child;}
	const crBoneData *GetNext() const		{return Next;}
	const crBoneData *GetParent() const		{return ParentPtr;}
	crBoneData *GetParent()					{return ParentPtr;}
	u32 GetDofs() const						{return Dofs;}
	u32 GetDOFs() const						{return Dofs;}

	//// DOF channel counts — from the dof mask (Rust Oni2BoneChannels: has_rot_*,
	//// has_trans_*; scale channels aren't tracked -> 0). ////
	int GetNumRotChannels() const	{return crCountBits(Dofs & (ROTATE_X|ROTATE_Y|ROTATE_Z));}
	int GetNumTransChannels() const	{return crCountBits(Dofs & (TRANSLATE_X|TRANSLATE_Y|TRANSLATE_Z));}
	int GetNumScaleChannels() const	{return crCountBits(Dofs & (SCALE_X|SCALE_Y|SCALE_Z));}

	const char* GetName() const				{return Name ? Name : "";}
	void Transform(const class Matrix34 *parentGlobal, class crBone *boneArray) const;

	// Resource records carry a dof mask at +0x10 whose bit assignment is not this
	// enum's (Remix vehicle bones read 0x238e, 0x3f8e, 0x2f8e...) and per-bone
	// rotate / translate channel counts at +0x3d / +0x3e (3/3 on almost every
	// vehicle bone, 0/0 on channel-less roots).  Returns a mask in this enum's
	// convention: the stored mask when it already decodes to those counts,
	// otherwise one synthesised from the counts (first N rotate / translate bits,
	// locked-rotation / locked-scale flags carried over).
	static u16 ResolveResourceDofs(u16 mask, u8 numRotChannels, u8 numTransChannels);

	int GetIndex() const			{return (int)Index;}
	int GetMirrorIndex() const		{return (int)MirrorIndex;}
	int GetMirrorOffset() const		{return (int)MirrorOffset;}
	// Rest rotation (XYZ eulers, radians) applied by crSkeleton::Init; the vehicle
	// exporter also stores the popup-headlight end angle here (rmcarmodeltype.cpp
	// reads it as GetRotationMax().x), so the old accessor names stay.
	const Vector3 &GetRotation() const		{return Rotation;}
	const Vector3 &GetDefaultRotation() const {return Rotation;}
	Vector3 GetRotationMin() const			{return Vector3(0.0f, 0.0f, 0.0f);}
	const Vector3 &GetRotationMax() const	{return Rotation;}
	// Absolute model-space rest position cached by the resource exporter (zero for
	// text-loaded skeletons); NOT the parent-relative offset.
	const Vector3 &GetRestPosition() const	{return RestPosition;}

	// Layout of the 0x44-byte PS2 record, verified against the Remix vehicle packs
	// and the alpha's crSkeleton::Init (which takes d from +0x20, the 3x3 from
	// FromEulersXYZ(+0x2c) and the parent from +0x1c -> +0x38).
	Vector3 RestPosition;	// +0x00  absolute rest position (exporter cache)
	const char *Name;		// +0x0c  pointer to name string
	u16 Dofs;				// +0x10  dof mask
	crBoneData *Next;		// +0x14  next sibling  (NULL = last)
	crBoneData *Child;		// +0x18  first child   (NULL = leaf)
	crBoneData *ParentPtr;	// +0x1c  parent bone pointer
	Vector3 Offset;			// +0x20  rest translation relative to the parent
	Vector3 Rotation;		// +0x2c  rest rotation, XYZ eulers
	u16 Index;				// +0x38  this bone's index
	u8 MirrorIndex;			// +0x3c  symmetric (mirror) bone index; 0xff = none in resources
	u8 MirrorOffset;		// +0x3d  (resources: TRANSLATE channel count, see ResolveResourceDofs)
	u8 Flags;				// +0x3e  (resources: ROTATE channel count)
	u8 Pad;					// +0x3f
	crBoneData *Mirror;		// +0x40  mirror bone pointer
	int Parent;				// runtime parent index (-1 = root)
};

////////////////////////////////////////////////////////////////////////////
//
// crSkeletonData — the STATIC skeleton: the array of crBoneData.
//
////////////////////////////////////////////////////////////////////////////

class crSkeletonData
{
public:
	crSkeletonData()
		: NumBones(0)
		, NumRotDOFs(0)
		, NumTransDOFs(0)
		, NumDOFs(0)
		, Simple(0)
		, Bones(nullptr)
		, m_RefCount(0)
	{}
	crSkeletonData(class datResource &rsc);

	~crSkeletonData() {
		if (Bones) {
			delete[] Bones;
			Bones = nullptr;
		}
	}

	// Load the static skeleton from a .skel file.  Returns false on failure.
	// (Implementation follows the Rust Oni2Rebuilt skeleton parser: parent
	// indices, local offsets, per-bone channel/DOF layout.)
	bool Load(const char *filename);
	bool Load(class Stream *s, const char *name = nullptr, bool closeStream = true);
	bool LoadFromResource(const class datResourceImage &image, u32 rootBoneAddr);
	bool Save(const char *filename) const;
	bool Save(class Stream *s) const;

	static crSkeletonData * GetSkeletonData(const char *filename) {
		if (!filename || !*filename) return nullptr;
		crSkeletonData *sd = new crSkeletonData();
		if (sd->Load(filename)) {
			sd->AddRef();
			return sd;
		}
		delete sd;
		return nullptr;
	}
	static crSkeletonData * Create(class Stream *s, const char *name = nullptr) {
		if (s) {
			crSkeletonData *sd = new crSkeletonData();
			if (sd->Load(s, name, false)) {
				sd->AddRef();
				return sd;
			}
			delete sd;
			return nullptr;
		}
		return GetSkeletonData(name);
	}
	void AddRef() { ++m_RefCount; }
	void Release() {
		if (--m_RefCount <= 0)
			delete this;
	}
	int GetRefCount() const { return m_RefCount; }

	crBoneData *FindBone(const char *name) {
		for (int i = 0; i < (int)NumBones; ++i) {
			if (Bones[i].Name && strcmp(Bones[i].Name, name) == 0) {
				return &Bones[i];
			}
		}
		return nullptr;
	}
	const crBoneData *FindBone(const char *name) const {
		for (int i = 0; i < (int)NumBones; ++i) {
			if (Bones[i].Name && strcmp(Bones[i].Name, name) == 0) {
				return &Bones[i];
			}
		}
		return nullptr;
	}

	int GetNumBones() const					{return (int)NumBones;}
	crBoneData *GetBones()					{return Bones;}
	const crBoneData *GetBones() const		{return Bones;}

	crBoneData *GetBone(int i)				{return &Bones[i];}
	const crBoneData *GetBone(int i) const	{return &Bones[i];}

	int IndexOf(const crBoneData &bd) const	{return (int)(&bd - Bones);}

	//// DOF totals — summed over bones.  GetNumDOFs == total animation channels
	//// (Rust: Oni2Skeleton::expected_anim_channels == channel_is_rot.len()). ////
	int GetNumDOFs() const					{return (int)NumDOFs;}
	int GetNumRotationDOFs() const			{return (NumDOFs > 0) ? (int)NumRotDOFs : 3;}
	int GetNumTranlationDOFs() const		{return (NumDOFs > 0) ? (int)NumTransDOFs : 3;}	// (sic: engine spelling)
	int GetNumScaleDOFs() const				{return 0;}
	bool IsSimple() const					{return Simple != 0;}

	operator const crSkeletonData*() const { return this; }
	operator crSkeletonData*() { return this; }

	u16 NumBones;			// +0x00
	u8 NumRotDOFs;			// +0x02
	u8 NumTransDOFs;		// +0x03
	u16 NumDOFs;			// +0x04 total rot+trans channels across all bones
	u16 Simple;				// +0x06 true if every bone is rotation-only (default DOFs)
	crBoneData *Bones;		// +0x08 array of bones (ptr fixed up on page-in)
	int m_RefCount;
};

#endif // CRSKELETON_SKELDATA_H
