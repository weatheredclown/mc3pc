#ifndef CRFANIMATION_ANIMATION_H
#define CRFANIMATION_ANIMATION_H

////////////////////////////////////////
// crfanimation/animation.h
//
// crfAnimation - a fixed-point animation: a run of crfAnimFrames plus the
// stride and per-frame root deltas.  Loaded from the ANI1 files under
// assets/anim (float channels on disk, quantised at load; see frame.h).
// Looked-up animations are cached by "<subfolder>/<name>" and ref-counted.
//
// Standalone twin of cranimation (never includes it); selected by the game
// with __USE_CRFANIMATION_LIB.
////////////////////////////////////////

#include "core/types.h"
#include "crfanimation/frame.h"
#include "crfanimation/fx.h"
#include "vector/vector3.h"

class Stream;
class crSkeletonData;
class ResourcePackage;

class crfAnimation {
public:
	enum {
		LOOP = 0x01,
		NORMALIZED = 0x02,
		ISCHANANIMATION = 0x04,
		PRENORMALIZED = 0x08,
		DELTASCOMPUTED = 0x20
	};

	crfAnimation();
	crfAnimation(int numFrames, int numChannels);
	virtual ~crfAnimation();

	//// Cache ////
	static void InitHashTables(ResourcePackage *pkg = NULL);
	static void ShutdownHashTables();

	static bool AnimExists(const char *name);
	static bool AnimExists(const char *subfolder, const char *name);

	// Lookup-or-load.  probeOnly suppresses the missing-file warning.
	static crfAnimation *GetAnimation(const char *name, const crSkeletonData *skeldata = NULL, bool probeOnly = false);
	static crfAnimation *GetAnimation(const char *name, bool probeOnly) { return GetAnimation(name, (const crSkeletonData *)NULL, probeOnly); }
	static crfAnimation *GetAnimationPrefix(const char *subfolder, const char *name, const crSkeletonData *skeldata = NULL, bool probeOnly = false, bool computeDeltas = true, bool useHashTable = true);
	static crfAnimation *GetAnimationPrefix(const char *subfolder, const char *name, bool probeOnly, bool computeDeltas = true, bool useHashTable = true)
		{ return GetAnimationPrefix(subfolder, name, (const crSkeletonData *)NULL, probeOnly, computeDeltas, useHashTable); }
	static crfAnimation *GetChanAnimation(const char *name);
	static bool RegisterAnimation(const char *name, crfAnimation *anim);

	//// Load ////
	bool LoadAnim(const char *subfolder, const char *name, const crSkeletonData *skeldata = NULL, bool probeOnly = false);
	void LoadFX(const char *filename);

	//// Access ////
	int GetNumFrames() const { return NumFrames; }
	int GetNumChannels() const { return NumChannels; }
	u16 GetFlags() const { return Flags; }
	bool GetLoop() const { return (Flags & LOOP) != 0; }
	void SetLoop(bool b) { if (b) Flags |= LOOP; else Flags &= (u16)~LOOP; }

	const crfAnimFrame &GetFrame(int f) const { return Frames[f]; }
	crfAnimFrame &GetFrame(int f) { return Frames[f]; }
	void GetFrame(crfAnimFrame &frm, int f) const { if (f >= 0 && f < NumFrames) frm = Frames[f]; }
	// Nearest frame for a fractional frame number.
	const crfAnimFrame &GetAnimFrame(float f) const;
	// Interpolated frame at phase t (0..1 over the animation; loops wrap).
	crfAnimFrame &GetBlendFrame(crfAnimFrame &frm, float t) const;
	crfAnimFrame &GetBlendFrame(crfAnimFrame &frm, float t, const crSkeletonData *skel, int loopOverride = -1) const;

	const Vector3 &GetStride() const { return Stride; }
	float GetStrideLength() const { return Stride.Mag(); }
	const Vector3 &GetFrameDelta(int frame) const;
	const Vector3 GetFrameDelta(float oldPhase, float newPhase) const;

	float GetRate() const { return (NumFrames > 0) ? (30.0f / (float)NumFrames) : 1.0f; }
	float GetAverageSpeed() const;

	//// Operations ////
	void Normalize(bool excludeX = false, bool excludeY = false, bool excludeZ = false);
	void ZeroX();
	void ZeroY();
	void ZeroDeltas();
	void ComputeFrameDeltas();

	//// Ref counting ////
	void AddRef() { ++RefCount; }
	void Release(bool freeMem = true) {
		if (RefCount > 0) {
			if (--RefCount == 0 && freeMem) {
				delete this;
			}
		}
	}
	int GetRefCount() const { return RefCount; }

	const crfAnimFXData *GetFXData() const { return FXData; }

protected:
	bool LoadAnim_ANI1(Stream *f, const crSkeletonData *skeldata);

	u16 NumFrames;
	u16 NumChannels;
	u16 Flags;
	Vector3 Stride;
	crfAnimFrame *Frames;
	crfAnimFXData *FXData;
	int RefCount;
};

#endif // CRFANIMATION_ANIMATION_H
