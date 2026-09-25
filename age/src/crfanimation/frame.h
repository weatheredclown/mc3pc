#ifndef CRFANIMATION_FRAME_H
#define CRFANIMATION_FRAME_H

////////////////////////////////////////
// crfanimation/frame.h
//
// crfAnimFrame - one frame of a FIXED-POINT animation (AGE 2.72 crfanimation).
//
// crfanimation is the fixed-point twin of cranimation: the same animation
// files (ANI1, float channels) are quantised to 16-bit channels at load
// time, which halves the storage and lets blends run as integer lerps.
// The game selects it with __USE_CRFANIMATION_LIB (mcped / mcprop) and
// names the two public classes crfAnimation / crfAnimFrame; the library is
// self-contained and never includes cranimation.
//
// Encoding (the game's own angle-fixup code relies on it: 65536 == 2*PI,
// 32767 == PI, 16384 == PI/2):
//   rotation channels     radians * 32768 / PI, wrapping naturally in 16 bits
//   translation channels  units * 32767 / 60   (+/-60 world units)
//
// Channel layout (no skeleton given): 0..2 root translation, then Euler
// rotation triples per bone.  With a skeleton the translation / rotation /
// scale DOF counts come from the crSkeletonData.  Typed accessors take and
// return floats; GetData() exposes the raw shorts.
////////////////////////////////////////

#include "core/types.h"
#include "vector/vector3.h"

class Stream;
class crSkeleton;
class crSkeletonData;
class datTokenizer;
class datResource;

#define CRF_ANIM_ROT_SCALE   (32768.0f / 3.14159265f)
#define CRF_ANIM_TRANS_RANGE 60.0f

inline float crfRotToFloat(short v)   { return (float)v * (1.0f / CRF_ANIM_ROT_SCALE); }
inline short crfFloatToRot(float f)   { return (short)(int)(f * CRF_ANIM_ROT_SCALE); }   // int -> short wraps: 2*PI == 0
inline float crfTransToFloat(short v) { return (float)v * (CRF_ANIM_TRANS_RANGE / 32767.0f); }
inline short crfFloatToTrans(float f) { float s = f * (32767.0f / CRF_ANIM_TRANS_RANGE); return (short)(s < -32767.0f ? -32767.0f : (s > 32767.0f ? 32767.0f : s)); }

class crfAnimFrame {
public:
	enum { FRAME_FORMAT_NORMAL, FRAME_FORMAT_TRACKER, NUM_FRAME_FORMATS };

	crfAnimFrame();
	crfAnimFrame(const crfAnimFrame &frm);
	explicit crfAnimFrame(int numChannels);
	crfAnimFrame(datResource &rsc);      // resource-image fixup (channels not owned)
	virtual ~crfAnimFrame();

	const crfAnimFrame &operator=(const crfAnimFrame &frm);

	// Allocate `size` channels (zeroed).  Re-Init with a smaller size keeps
	// the allocation.
	void Init(int size);

	//// Load / save ////
	// Reads NumChannels floats from the stream and quantises them.  The
	// skeleton (optional) fixes the translation-channel count; flags are the
	// ANI1 header flags.
	void LoadBin(Stream *f, int size, const crSkeletonData *skeldata, int formatFlags = 0);
	void LoadAscii(datTokenizer &T, int size);
	void SaveBin(Stream *f, const crSkeletonData *skeldata, unsigned int formatFlags = 0) const;

	//// Access ////
	int GetNumChannels() const { return NumChannels; }
	int GetNumBones() const { return NumChannels / 3; }
	int GetNumTransChannels() const { return NumTransChannels; }
	void SetNumTransChannels(int n) { NumTransChannels = (short)n; }

	// Raw fixed-point channels ("not for the faint").
	short *GetData() { return Data; }
	const short *GetData() const { return Data; }
	short *GetFPData() { return Data; }
	const short *GetFPData() const { return Data; }
	const short &GetFPData(int d) const { return Data[d]; }

	// Typed accessors: the caller knows what the channel holds.
	float GetRotData(int d) const   { return crfRotToFloat(Data[d]); }
	float GetTransData(int d) const { return crfTransToFloat(Data[d]); }
	float GetScaleData(int d) const { return crfRotToFloat(Data[d]); }
	void SetRotData(int d, float v)   { Data[d] = crfFloatToRot(v); }
	void SetTransData(int d, float v) { Data[d] = crfFloatToTrans(v); }
	void SetScaleData(int d, float v) { Data[d] = crfFloatToRot(v); }

	// Layout-aware accessors: translation channels use the translation
	// range, everything else the rotation range.
	float GetData(int d) const { return IsTransChannel(d) ? GetTransData(d) : GetRotData(d); }
	void SetData(int d, float v) { if (IsTransChannel(d)) SetTransData(d, v); else SetRotData(d, v); }
	void ZeroData(int d) { Data[d] = 0; }

	const Vector3 GetVector(int d = 0) const;
	void SetVector(int d, const Vector3 &v);
	const Vector3 GetRotVector(int d) const;
	const Vector3 GetTransVector(int d) const;
	void SetRotVector(int d, const Vector3 &v);
	void SetTransVector(int d, const Vector3 &v);

	const Vector3 &GetDelta() const { return Delta; }
	void SetDelta(const Vector3 &v) { Delta = v; }

	//// Operations ////
	void ZeroX(float offset = 0.0f) { Data[0] = (short)(Data[0] - crfFloatToTrans(offset)); }
	void ZeroY(float offset = 0.0f) { Data[1] = (short)(Data[1] - crfFloatToTrans(offset)); }
	void ZeroZ(float offset = 0.0f) { Data[2] = (short)(Data[2] - crfFloatToTrans(offset)); }

	void Identity();
	void Copy(const crfAnimFrame &frm, int first = -1, int last = -1);
	void Set(const crfAnimFrame &frm) { Copy(frm); }
	void Add(const crfAnimFrame &frm, int first = -1, int last = -1);
	void AddScaled(const crfAnimFrame &frm, float scale, int first = -1, int last = -1);
	void Subtract(const crfAnimFrame &frm) { AddScaled(frm, -1.0f); }

	// this = lerp(frm1, frm2, t); rotation channels take the short way round.
	const crfAnimFrame &Blend(float t, const crfAnimFrame &frm1, const crfAnimFrame &frm2, const crSkeletonData *skel = NULL, int first = -1, int last = -1);
	const crfAnimFrame &BlendSimple(float t, const crfAnimFrame &frm1, const crfAnimFrame &frm2);
	static void Blend(float t, const crfAnimFrame &frm1, const crfAnimFrame &frm2, crfAnimFrame &out) { out.Blend(t, frm1, frm2); }

	// Apply to / capture from a skeleton (bone-local matrices + Euler cache).
	void Pose(crSkeleton &skel, bool applyRootTrans = true) const;
	void Pose(crSkeleton *skel) const { if (skel) Pose(*skel); }
	void SetFromPose(const crSkeleton &skel);

private:
	bool IsTransChannel(int d) const { return d < NumTransChannels; }

	short *Data;
	int NumChannels;
	int MaxNumChannels;
	short NumTransChannels;
	bool Allocated;
	Vector3 Delta;
};

typedef crfAnimFrame crfFrame;

#endif // CRFANIMATION_FRAME_H
