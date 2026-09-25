#include "crfanimation/frame.h"

#include "core/assert.h"
#include "core/output.h"
#include "core/stream.h"
#include "crskeleton/skeleton.h"
#include "crskeleton/skeldata.h"
#include "data/resource.h"
#include "data/token.h"
#include "vector/matrix34.h"

#include <math.h>
#include <string.h>

// t and omt are 0..16384 fixed-point weights.
static inline short sBiLerp(int omt, int a, int t, int b) {
	return (short)((a * omt + b * t) >> 14);
}

// Angles wrap in 16 bits (65536 == 2*PI), so the signed 16-bit difference is
// already the short way round; lerping along it never crosses the seam.
static inline short sWrapBiLerp(int /*omt*/, int a, int t, int b) {
	int d = (short)(b - a);
	return (short)(a + ((d * t) >> 14));
}

// Wrap a rotation sum back into 16 bits; clamp a translation sum.
static inline short sWrapRot(int v) { return (short)v; }
static inline short sClamp(int v) { return (short)(v > 32767 ? 32767 : (v < -32767 ? -32767 : v)); }

crfAnimFrame::crfAnimFrame()
	: Data(NULL), NumChannels(0), MaxNumChannels(0), NumTransChannels(3), Allocated(false)
{
	Delta.Set(0.0f, 0.0f, 0.0f);
}

crfAnimFrame::crfAnimFrame(int numChannels)
	: Data(NULL), NumChannels(0), MaxNumChannels(0), NumTransChannels(3), Allocated(false)
{
	Delta.Set(0.0f, 0.0f, 0.0f);
	Init(numChannels);
}

crfAnimFrame::crfAnimFrame(const crfAnimFrame &frm)
	: Data(NULL), NumChannels(0), MaxNumChannels(0), NumTransChannels(3), Allocated(false)
{
	Delta.Set(0.0f, 0.0f, 0.0f);
	*this = frm;
}

// Resource-image frame: the channel block lives inside the resource, so it
// is only fixed up (relative offset -> pointer), never owned or freed.
crfAnimFrame::crfAnimFrame(datResource &rsc)
{
	rsc.PointerFixup(Data);
	Allocated = false;
	MaxNumChannels = NumChannels;
}

crfAnimFrame::~crfAnimFrame()
{
	if (Allocated) delete[] Data;
}

const crfAnimFrame &crfAnimFrame::operator=(const crfAnimFrame &frm)
{
	if (this == &frm) return *this;
	Init(frm.NumChannels);
	if (frm.NumChannels > 0) memcpy(Data, frm.Data, frm.NumChannels * sizeof(short));
	NumTransChannels = frm.NumTransChannels;
	Delta = frm.Delta;
	return *this;
}

void crfAnimFrame::Init(int size)
{
	if (size < 0) size = 0;
	if (size > MaxNumChannels) {
		if (Allocated) delete[] Data;
		Data = new short[size];
		MaxNumChannels = size;
		Allocated = true;
	}
	NumChannels = size;
	if (size > 0) memset(Data, 0, size * sizeof(short));
	Delta.Set(0.0f, 0.0f, 0.0f);
}

//// Load / save ////////////////////////////////////////////////////////////////

void crfAnimFrame::LoadBin(Stream *f, int size, const crSkeletonData *skeldata, int /*formatFlags*/)
{
	Init(size);
	if (skeldata && !skeldata->IsSimple() && skeldata->GetNumDOFs() > 0)
		NumTransChannels = (short)skeldata->GetNumTranlationDOFs();
	else
		NumTransChannels = (short)(size < 3 ? size : 3);

	if (size <= 0) return;
	float *tmp = new float[size];
	f->ReadFloat(tmp, size);
	for (int i = 0; i < size; i++)
		SetData(i, tmp[i]);
	delete[] tmp;
}

void crfAnimFrame::LoadAscii(datTokenizer &T, int size)
{
	Init(size);
	NumTransChannels = (short)(size < 3 ? size : 3);
	for (int i = 0; i < size; i++)
		SetData(i, T.GetFloat());
}

void crfAnimFrame::SaveBin(Stream *f, const crSkeletonData * /*skeldata*/, unsigned int /*formatFlags*/) const
{
	for (int i = 0; i < NumChannels; i++) {
		float v = GetData(i);
		f->WriteFloat(&v, 1);
	}
}

//// Vector access //////////////////////////////////////////////////////////////

const Vector3 crfAnimFrame::GetVector(int d) const
{
	return Vector3(GetData(d), GetData(d + 1), GetData(d + 2));
}

void crfAnimFrame::SetVector(int d, const Vector3 &v)
{
	SetData(d, v.x); SetData(d + 1, v.y); SetData(d + 2, v.z);
}

const Vector3 crfAnimFrame::GetRotVector(int d) const
{
	return Vector3(GetRotData(d), GetRotData(d + 1), GetRotData(d + 2));
}

const Vector3 crfAnimFrame::GetTransVector(int d) const
{
	return Vector3(GetTransData(d), GetTransData(d + 1), GetTransData(d + 2));
}

void crfAnimFrame::SetRotVector(int d, const Vector3 &v)
{
	SetRotData(d, v.x); SetRotData(d + 1, v.y); SetRotData(d + 2, v.z);
}

void crfAnimFrame::SetTransVector(int d, const Vector3 &v)
{
	SetTransData(d, v.x); SetTransData(d + 1, v.y); SetTransData(d + 2, v.z);
}

//// Operations /////////////////////////////////////////////////////////////////

void crfAnimFrame::Identity()
{
	if (NumChannels > 0) memset(Data, 0, NumChannels * sizeof(short));
	Delta.Set(0.0f, 0.0f, 0.0f);
}

static inline void sRange(int numChannels, int &first, int &last)
{
	if (first < 0) first = 0;
	if (last < 0 || last >= numChannels) last = numChannels - 1;
}

void crfAnimFrame::Copy(const crfAnimFrame &frm, int first, int last)
{
	if (NumChannels < frm.NumChannels) Init(frm.NumChannels);
	NumTransChannels = frm.NumTransChannels;
	int n = frm.NumChannels;
	sRange(n, first, last);
	for (int i = first; i <= last; i++) Data[i] = frm.Data[i];
	Delta = frm.Delta;
}

void crfAnimFrame::Add(const crfAnimFrame &frm, int first, int last)
{
	int n = NumChannels < frm.NumChannels ? NumChannels : frm.NumChannels;
	sRange(n, first, last);
	for (int i = first; i <= last; i++) {
		int v = Data[i] + frm.Data[i];
		Data[i] = IsTransChannel(i) ? sClamp(v) : sWrapRot(v);
	}
}

void crfAnimFrame::AddScaled(const crfAnimFrame &frm, float scale, int first, int last)
{
	int n = NumChannels < frm.NumChannels ? NumChannels : frm.NumChannels;
	sRange(n, first, last);
	for (int i = first; i <= last; i++) {
		int v = Data[i] + (int)((float)frm.Data[i] * scale);
		Data[i] = IsTransChannel(i) ? sClamp(v) : sWrapRot(v);
	}
}

const crfAnimFrame &crfAnimFrame::BlendSimple(float t, const crfAnimFrame &frm1, const crfAnimFrame &frm2)
{
	return Blend(t, frm1, frm2);
}

const crfAnimFrame &crfAnimFrame::Blend(float t, const crfAnimFrame &frm1, const crfAnimFrame &frm2, const crSkeletonData * /*skel*/, int first, int last)
{
	Assert(frm1.NumChannels == frm2.NumChannels);
	if (t <= 0.0f) return *this = frm1;
	if (t >= 1.0f) return *this = frm2;

	if (MaxNumChannels < frm1.NumChannels) Init(frm1.NumChannels);
	NumChannels = frm1.NumChannels;
	NumTransChannels = frm1.NumTransChannels;

	int ft = (int)(t * 16384.0f);
	int omft = 16384 - ft;
	sRange(NumChannels, first, last);
	for (int i = first; i <= last; i++) {
		if (IsTransChannel(i))
			Data[i] = sBiLerp(omft, frm1.Data[i], ft, frm2.Data[i]);
		else
			Data[i] = sWrapBiLerp(omft, frm1.Data[i], ft, frm2.Data[i]);
	}
	Delta.x = frm1.Delta.x * (1.0f - t) + frm2.Delta.x * t;
	Delta.y = frm1.Delta.y * (1.0f - t) + frm2.Delta.y * t;
	Delta.z = frm1.Delta.z * (1.0f - t) + frm2.Delta.z * t;
	return *this;
}

//// Skeleton ///////////////////////////////////////////////////////////////////

void crfAnimFrame::Pose(crSkeleton &skel, bool applyRootTrans) const
{
	if (!Data || NumChannels == 0) return;

	// Single-channel animation: a Y rotation on the root (hinged props).
	if (NumChannels == 1) {
		float y = GetRotData(0);
		crBone &root = skel.GetBone(0);
		root.GetEuler().Set(0.0f, y, 0.0f);
		Matrix34 &m = root.GetLocalMtx();
		Vector3 d = m.d;
		m.MakeRotateY(y);
		m.d = d;
		return;
	}

	const crSkeletonData &skelData = skel.GetSkeletonData();
	int boneCount = skelData.GetNumBones();

	if (skelData.IsSimple()) {
		for (int i = 0; i < boneCount; i++) {
			if (i * 3 + 5 >= NumChannels) break;
			Vector3 e = GetRotVector(i * 3 + 3);
			skel.GetBone(i).GetEuler().Set(e);
			skel.GetBone(i).GetLocalMtx().FromEulersXZY(e);
		}
		if (applyRootTrans) skel.GetBone(0).GetLocalMtx().d = GetTransVector(0);
		return;
	}

	int rotDataIndex = skelData.GetNumTranlationDOFs();
	int scaleDataIndex = rotDataIndex + skelData.GetNumRotationDOFs();
	int transDataIndex = 0;
	Vector3 dataV;

	for (int i = 0; i < boneCount; i++) {
		crBone &bone = skel.GetBone(i);
		u32 dofs = bone.GetDOFs();

		if (dofs == crBoneData::DEFAULT_DOFS) {
			if (rotDataIndex + 2 >= NumChannels) break;
			dataV.x = GetRotData(rotDataIndex++);
			dataV.y = GetRotData(rotDataIndex++);
			dataV.z = GetRotData(rotDataIndex++);
			bone.GetEuler().Set(dataV);
			bone.GetLocalMtx().FromEulersXZY(dataV);
			continue;
		}

		Vector3 &d = bone.GetLocalMtx().d;
		if (dofs & crBoneData::TRANSLATE_X) d.x = GetTransData(transDataIndex++);
		if (dofs & crBoneData::TRANSLATE_Y) d.y = GetTransData(transDataIndex++);
		if (dofs & crBoneData::TRANSLATE_Z) d.z = GetTransData(transDataIndex++);

		dataV.Set(0.0f, 0.0f, 0.0f);
		if (dofs & crBoneData::ROTATE_X) dataV.x = GetRotData(rotDataIndex++);
		if (dofs & crBoneData::ROTATE_Y) dataV.y = GetRotData(rotDataIndex++);
		if (dofs & crBoneData::ROTATE_Z) dataV.z = GetRotData(rotDataIndex++);
		bone.GetEuler().Set(dataV);
		Vector3 keep = bone.GetLocalMtx().d;
		bone.GetLocalMtx().FromEulersXZY(dataV);
		bone.GetLocalMtx().d = keep;

		// Scale DOFs are consumed but not applied (same as cranimation).
		if (dofs & crBoneData::SCALE_X) scaleDataIndex++;
		if (dofs & crBoneData::SCALE_Y) scaleDataIndex++;
		if (dofs & crBoneData::SCALE_Z) scaleDataIndex++;
	}
}

void crfAnimFrame::SetFromPose(const crSkeleton &skel)
{
	const crSkeletonData &skelData = skel.GetSkeletonData();
	int boneCount = skelData.GetNumBones();

	if (skelData.IsSimple()) {
		if (NumChannels < boneCount * 3 + 3) Init(boneCount * 3 + 3);
		NumTransChannels = 3;
		for (int i = 0; i < boneCount; i++) {
			Vector3 euler;
			skel.GetBone(i).GetLocalMtx().ToEulersXZY(euler);
			SetRotVector(i * 3 + 3, euler);
		}
		SetTransVector(0, skel.GetBone(0).GetLocalMtx().d);
		return;
	}

	int numTrans = skelData.GetNumTranlationDOFs();
	int rotDataIndex = numTrans;
	int transDataIndex = 0;
	if (NumChannels < skelData.GetNumDOFs()) Init(skelData.GetNumDOFs());
	NumTransChannels = (short)numTrans;

	for (int i = 0; i < boneCount; i++) {
		const crBone &bone = skel.GetBone(i);
		u32 dofs = bone.GetDOFs();
		Vector3 euler;
		bone.GetLocalMtx().ToEulersXZY(euler);
		if (dofs == crBoneData::DEFAULT_DOFS) {
			SetRotData(rotDataIndex++, euler.x);
			SetRotData(rotDataIndex++, euler.y);
			SetRotData(rotDataIndex++, euler.z);
			continue;
		}
		const Vector3 &d = bone.GetLocalMtx().d;
		if (dofs & crBoneData::TRANSLATE_X) SetTransData(transDataIndex++, d.x);
		if (dofs & crBoneData::TRANSLATE_Y) SetTransData(transDataIndex++, d.y);
		if (dofs & crBoneData::TRANSLATE_Z) SetTransData(transDataIndex++, d.z);
		if (dofs & crBoneData::ROTATE_X) SetRotData(rotDataIndex++, euler.x);
		if (dofs & crBoneData::ROTATE_Y) SetRotData(rotDataIndex++, euler.y);
		if (dofs & crBoneData::ROTATE_Z) SetRotData(rotDataIndex++, euler.z);
	}
}
