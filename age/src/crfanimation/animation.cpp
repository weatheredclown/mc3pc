#include "crfanimation/animation.h"

#include "core/assert.h"
#include "core/output.h"
#include "core/stream.h"
#include "crskeleton/skeldata.h"
#include "data/assetcfg.h"
#include "data/token.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

////////////////////////////////////////////////////////////////////////////////
// Name cache: chained hash keyed by "<subfolder>/<name>" (case-insensitive).

namespace {

struct sEntry {
	char *Key;
	crfAnimation *Anim;
	sEntry *Next;
};

const int kNumBuckets = 256;

struct sTable {
	sEntry *Buckets[kNumBuckets];

	sTable() { memset(Buckets, 0, sizeof(Buckets)); }
	~sTable() {
		for (int i = 0; i < kNumBuckets; i++) {
			sEntry *e = Buckets[i];
			while (e) {
				sEntry *n = e->Next;
				if (e->Anim) e->Anim->Release();
				delete[] e->Key;
				delete e;
				e = n;
			}
		}
	}

	static unsigned Hash(const char *s) {
		unsigned h = 5381;
		for (; *s; s++) {
			char c = *s;
			if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
			if (c == '\\') c = '/';
			h = h * 33 + (unsigned char)c;
		}
		return h;
	}
	static bool Same(const char *a, const char *b) {
		for (;; a++, b++) {
			char ca = *a, cb = *b;
			if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
			if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
			if (ca == '\\') ca = '/';
			if (cb == '\\') cb = '/';
			if (ca != cb) return false;
			if (!ca) return true;
		}
	}

	crfAnimation *Access(const char *key) const {
		for (sEntry *e = Buckets[Hash(key) % kNumBuckets]; e; e = e->Next)
			if (Same(e->Key, key)) return e->Anim;
		return NULL;
	}
	void Insert(const char *key, crfAnimation *anim) {
		unsigned b = Hash(key) % kNumBuckets;
		sEntry *e = new sEntry;
		e->Key = new char[strlen(key) + 1];
		strcpy(e->Key, key);
		e->Anim = anim;
		e->Next = Buckets[b];
		Buckets[b] = e;
		anim->AddRef();
	}
};

sTable *sAnimTable = NULL;
sTable *sChanTable = NULL;

void sMakeKey(char *key, int size, const char *subfolder, const char *name)
{
	if (subfolder && subfolder[0])
		snprintf(key, size, "%s/%s", subfolder, name);
	else
		snprintf(key, size, "%s", name);
	key[size - 1] = 0;
}

} // namespace

////////////////////////////////////////////////////////////////////////////////

crfAnimation::crfAnimation()
	: NumFrames(0), NumChannels(0), Flags(0), Frames(NULL), FXData(NULL), RefCount(0)
{
	Stride.Set(0.0f, 0.0f, 0.0f);
}

crfAnimation::crfAnimation(int numFrames, int numChannels)
	: NumFrames((u16)numFrames), NumChannels((u16)numChannels), Flags(0), Frames(NULL), FXData(NULL), RefCount(0)
{
	Stride.Set(0.0f, 0.0f, 0.0f);
	if (numFrames > 0) {
		Frames = new crfAnimFrame[numFrames];
		for (int i = 0; i < numFrames; i++) Frames[i].Init(numChannels);
	}
}

crfAnimation::~crfAnimation()
{
	delete[] Frames;
	delete FXData;
}

//// Cache //////////////////////////////////////////////////////////////////////

void crfAnimation::InitHashTables(ResourcePackage * /*pkg*/)
{
	if (!sAnimTable) sAnimTable = new sTable;
	if (!sChanTable) sChanTable = new sTable;
}

void crfAnimation::ShutdownHashTables()
{
	delete sAnimTable; sAnimTable = NULL;
	delete sChanTable; sChanTable = NULL;
}

bool crfAnimation::AnimExists(const char *name)
{
	return ASSET.Exists(name, "anim");
}

bool crfAnimation::AnimExists(const char *subfolder, const char *name)
{
	if (!subfolder || !subfolder[0]) return AnimExists(name);
	ASSET.PushFolder(subfolder);
	bool exists = ASSET.Exists(name, "anim");
	ASSET.PopFolder();
	return exists;
}

crfAnimation *crfAnimation::GetAnimation(const char *name, const crSkeletonData *skeldata, bool probeOnly)
{
	return GetAnimationPrefix("", name, skeldata, probeOnly);
}

crfAnimation *crfAnimation::GetAnimationPrefix(const char *subfolder, const char *name, const crSkeletonData *skeldata, bool probeOnly, bool computeDeltas, bool useHashTable)
{
	if (!name || !name[0]) return NULL;
	if (!sAnimTable) InitHashTables();

	char key[256];
	sMakeKey(key, sizeof(key), subfolder, name);

	crfAnimation *anim = useHashTable ? sAnimTable->Access(key) : NULL;
	if (anim) {
		anim->AddRef();
		return anim;
	}

	anim = new crfAnimation;
	if (!anim->LoadAnim(subfolder ? subfolder : "", name, skeldata, probeOnly)) {
		delete anim;
		return NULL;
	}
	if (computeDeltas) anim->ComputeFrameDeltas();
	if (useHashTable) sAnimTable->Insert(key, anim);
	anim->AddRef();
	return anim;
}

crfAnimation *crfAnimation::GetChanAnimation(const char *name)
{
	if (!sChanTable) InitHashTables();
	crfAnimation *anim = sChanTable->Access(name);
	if (anim) {
		anim->AddRef();
		return anim;
	}
	anim = new crfAnimation;
	if (!anim->LoadAnim("", name, NULL, false)) {
		delete anim;
		return NULL;
	}
	anim->Flags |= ISCHANANIMATION;
	sChanTable->Insert(name, anim);
	anim->AddRef();
	return anim;
}

bool crfAnimation::RegisterAnimation(const char *name, crfAnimation *anim)
{
	if (!sAnimTable) InitHashTables();
	crfAnimation *existing = sAnimTable->Access(name);
	if (existing) {
		if (existing != anim) Errorf("crfAnimation: '%s' already registered by other data", name);
		return false;
	}
	sAnimTable->Insert(name, anim);
	return true;
}

//// Load ///////////////////////////////////////////////////////////////////////

// ANI1 layout (assets/anim/*.anim):
//   char[4] "ANI1"
//   u32     flags      bit0 = loop
//   u32     numFrames
//   u32     numChannels
//   float   stride[3]
//   float   channels[numFrames][numChannels]
// Every MC3 .anim file measures exactly 16 + 12 + numFrames*numChannels*4.
static const unsigned int ANI1_FLAG_LOOP = 1u;

bool crfAnimation::LoadAnim_ANI1(Stream *f, const crSkeletonData *skeldata)
{
	int flags = 0, nf = 0, nc = 0;
	f->ReadInt(&flags, 1);
	f->ReadInt(&nf, 1);
	f->ReadInt(&nc, 1);
	if (nf < 0 || nf > 65535 || nc < 0 || nc > 65535) {
		Errorf("crfAnimation: bad ANI1 header (%d frames, %d channels)", nf, nc);
		return false;
	}
	NumFrames = (u16)nf;
	NumChannels = (u16)nc;
	f->ReadFloat(&Stride.x, 1);
	f->ReadFloat(&Stride.y, 1);
	f->ReadFloat(&Stride.z, 1);

	if ((unsigned)flags & ANI1_FLAG_LOOP) Flags |= LOOP; else Flags &= (u16)~LOOP;

	delete[] Frames;
	Frames = new crfAnimFrame[NumFrames];
	for (int i = 0; i < NumFrames; i++)
		Frames[i].LoadBin(f, NumChannels, skeldata, flags);
	return true;
}

bool crfAnimation::LoadAnim(const char *subfolder, const char *name, const crSkeletonData *skeldata, bool probeOnly)
{
	if (subfolder && subfolder[0]) ASSET.PushFolder(subfolder);
	Stream *f = ASSET.Open(name, "anim", probeOnly);
	if (subfolder && subfolder[0]) ASSET.PopFolder();
	if (!f) return false;

	int magic = 0;
	f->ReadInt(&magic, 1);
	bool ok = false;
	if (magic == (('1' << 24) | ('I' << 16) | ('N' << 8) | 'A')) {
		ok = LoadAnim_ANI1(f, skeldata);
	} else if (magic == (('i' << 16) | ('n' << 8) | 'a')) {
		// Older float format: flags, frames, channels, stride (z only unless
		// bit0), loop byte, then frames.
		int fflags = 0, nf = 0, nc = 0;
		f->ReadInt(&fflags, 1);
		f->ReadInt(&nf, 1);
		f->ReadInt(&nc, 1);
		NumFrames = (u16)nf;
		NumChannels = (u16)nc;
		if (fflags & 1) {
			f->ReadFloat(&Stride.x, 1); f->ReadFloat(&Stride.y, 1); f->ReadFloat(&Stride.z, 1);
		} else {
			float s = 0.0f; f->ReadFloat(&s, 1); Stride.Set(0.0f, 0.0f, s);
		}
		if (f->GetCh() != 0) Flags |= LOOP; else Flags &= (u16)~LOOP;
		delete[] Frames;
		Frames = new crfAnimFrame[NumFrames];
		for (int i = 0; i < NumFrames; i++) Frames[i].LoadBin(f, NumChannels, skeldata, fflags);
		ok = true;
	} else {
		Errorf("crfAnimation::LoadAnim(%s) - unsupported format identifier 0x%08x", name, magic);
	}
	f->Close();
	if (ok) LoadFX(name);
	return ok;
}

void crfAnimation::LoadFX(const char *filename)
{
	if (!ASSET.Exists(filename, "animfx")) return;
	Stream *s = ASSET.Open(filename, "animfx");
	if (!s) return;
	datTokenizer tok;
	tok.Init(filename, s);
	FXData = crfAnimFXData::CreateInstance();
	if (FXData) FXData->Load(tok);
	s->Close();
}

//// Access /////////////////////////////////////////////////////////////////////

const crfAnimFrame &crfAnimation::GetAnimFrame(float f) const
{
	int i = (int)(f + 0.5f);
	if (i < 0) i = 0;
	if (i >= NumFrames) i = NumFrames - 1;
	return Frames[i];
}

crfAnimFrame &crfAnimation::GetBlendFrame(crfAnimFrame &frm, float t) const
{
	return GetBlendFrame(frm, t, NULL, -1);
}

crfAnimFrame &crfAnimation::GetBlendFrame(crfAnimFrame &frm, float t, const crSkeletonData *skel, int loopOverride) const
{
	if (NumFrames == 0) return frm;
	if (NumFrames == 1) { frm = Frames[0]; return frm; }

	bool loop = loopOverride < 0 ? GetLoop() : (loopOverride != 0);
	float span = loop ? (float)NumFrames : (float)(NumFrames - 1);
	float pos = t * span;
	if (loop) {
		pos = fmodf(pos, (float)NumFrames);
		if (pos < 0.0f) pos += (float)NumFrames;
	} else {
		if (pos < 0.0f) pos = 0.0f;
		if (pos > span) pos = span;
	}
	int i0 = (int)pos;
	float frac = pos - (float)i0;
	if (i0 >= NumFrames) i0 = NumFrames - 1;
	int i1 = i0 + 1;
	if (i1 >= NumFrames) i1 = loop ? 0 : NumFrames - 1;
	frm.Blend(frac, Frames[i0], Frames[i1], skel);
	return frm;
}

const Vector3 &crfAnimation::GetFrameDelta(int frame) const
{
	if (frame < 0) frame = 0;
	if (frame >= NumFrames) frame = NumFrames - 1;
	return Frames[frame].GetDelta();
}

const Vector3 crfAnimation::GetFrameDelta(float oldPhase, float newPhase) const
{
	Vector3 d(0.0f, 0.0f, 0.0f);
	if (NumFrames == 0) return d;
	if (newPhase < oldPhase) newPhase += 1.0f;   // wrapped
	int f0 = (int)(oldPhase * NumFrames);
	int f1 = (int)(newPhase * NumFrames);
	for (int f = f0 + 1; f <= f1; f++)
		d += Frames[f % NumFrames].GetDelta();
	return d;
}

float crfAnimation::GetAverageSpeed() const
{
	if (NumFrames == 0) return 0.0f;
	return GetStrideLength() * GetRate();
}

//// Operations /////////////////////////////////////////////////////////////////

void crfAnimation::Normalize(bool excludeX, bool excludeY, bool excludeZ)
{
	if (NumFrames == 0 || NumChannels < 3 || (Flags & NORMALIZED)) return;
	Vector3 base = Frames[0].GetTransVector(0);
	for (int i = 0; i < NumFrames; i++) {
		Vector3 v = Frames[i].GetTransVector(0);
		if (!excludeX) v.x -= base.x;
		if (!excludeY) v.y -= base.y;
		if (!excludeZ) v.z -= base.z;
		Frames[i].SetTransVector(0, v);
	}
	Flags |= NORMALIZED;
}

void crfAnimation::ZeroX()
{
	for (int i = 0; i < NumFrames; i++) Frames[i].ZeroData(0);
}

void crfAnimation::ZeroY()
{
	for (int i = 0; i < NumFrames; i++) Frames[i].ZeroData(1);
}

void crfAnimation::ZeroDeltas()
{
	Vector3 z(0.0f, 0.0f, 0.0f);
	for (int i = 0; i < NumFrames; i++) Frames[i].SetDelta(z);
	Flags &= (u16)~DELTASCOMPUTED;
}

void crfAnimation::ComputeFrameDeltas()
{
	if (NumFrames == 0 || NumChannels < 3) return;
	for (int i = 0; i < NumFrames; i++) {
		int prev = (i == 0) ? (GetLoop() ? NumFrames - 1 : 0) : i - 1;
		Vector3 d = Frames[i].GetTransVector(0) - Frames[prev].GetTransVector(0);
		if (i == 0 && GetLoop()) d += Stride;
		Frames[i].SetDelta(d);
	}
	Flags |= DELTASCOMPUTED;
}
