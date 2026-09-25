////////////////////////////////////////
// skeldata.cpp
//
// crSkeletonData::Load — parse the text .skel format into the static skeleton
// (crBoneData array).  Grammar (see the Rust Oni2Rebuilt reference,
// oni2_loader/parsers/skeleton.rs):
//
//     Version: <n>
//     NumBones <n>
//     bone <name> {
//         offset <x> <y> <z>          // local offset from parent
//         rotX [limit lo hi | lock]   // per-channel DOF flags
//         rotY ...  rotZ ...
//         transX ... transY ... transZ ...
//         bone <child> { ... }        // nested, depth-first pre-order
//     }
//
// Bones are numbered in depth-first pre-order (matches the anim channel order).
// A locked channel is present in the file but consumes no DOF, so its mask bit
// is left clear.
////////////////////////////////////////

#include "crskeleton/skeldata.h"
#include "data/assetcfg.h"
#include "data/token.h"
#include "core/output.h"
#include "atl/array.h"
#include <string.h>

// Map a channel keyword to its crBoneData dof-mask bit; returns -1 if not a
// channel token.
static int sChannelBit(const char *tok)
{
	if (!_stricmp(tok, "rotX"))   return crBoneData::ROTATE_X;
	if (!_stricmp(tok, "rotY"))   return crBoneData::ROTATE_Y;
	if (!_stricmp(tok, "rotZ"))   return crBoneData::ROTATE_Z;
	if (!_stricmp(tok, "transX")) return crBoneData::TRANSLATE_X;
	if (!_stricmp(tok, "transY")) return crBoneData::TRANSLATE_Y;
	if (!_stricmp(tok, "transZ")) return crBoneData::TRANSLATE_Z;
	return -1;
}

bool crSkeletonData::Load(const char *filename)
{
	Stream *s = ASSET.Open(filename, "skel");
	if (!s)
	{
		Errorf("crSkeletonData::Load: can't open '%s.skel'", filename ? filename : "(null)");
		return false;
	}
	return Load(s, filename, true);
}

static int sInitMirror(int n, crBoneData *Bones)
{
	int numMirrored = 0;
	int *starts = new int[n];
	int acc = 0;
	for (int i = 0; i < n; i++)
	{
		starts[i] = acc;
		acc += Bones[i].GetNumTransChannels() + Bones[i].GetNumRotChannels();
	}
	for (int i = 0; i < n; i++)
	{
		const char *nm = Bones[i].Name;
		int len = (int)strlen(nm);
		if (len < 2 || len >= 64) continue;
		char partner[64];
		if (!_stricmp(nm + len - 2, "_l"))      { strcpy(partner, nm); partner[len - 1] = 'r'; }
		else if (!_stricmp(nm + len - 2, "_r")) { strcpy(partner, nm); partner[len - 1] = 'l'; }
		else continue;							// symmetric bone -> leave self/0
		for (int j = 0; j < n; j++)
		{
			if (j != i && !_stricmp(Bones[j].Name, partner))
			{
				Bones[i].MirrorIndex  = j;
				Bones[i].MirrorOffset = starts[j] - starts[i];
				numMirrored++;
				break;
			}
		}
	}
	delete[] starts;
	return numMirrored;
}

bool crSkeletonData::Load(Stream *s, const char *filename, bool closeStream)
{
	if (!s)
		return false;

	datAsciiTokenizer tok;
	tok.Init(filename ? filename : "skel", s);

	char t[256];

	// --- Header: scan to "NumBones <n>" (skips "Version : <n>"). ---
	int n = 0;
	for (;;)
	{
		tok.GetToken(t, sizeof(t));
		if (t[0] == '\0') break;					// EOF before NumBones
		if (!_stricmp(t, "NumBones")) { n = tok.GetInt(); break; }
	}

	if (n <= 0)
	{
		Errorf("crSkeletonData::Load: '%s.skel' has no bones", filename);
		s->Close();
		return false;
	}

	NumBones = n;
	Bones = new crBoneData[n];

	// --- Body: walk the nested bone blocks with a parent-index stack. ---
	int parentStack[128];
	int depth = 0;
	parentStack[0] = -1;						// root's parent
	int next = 0;								// next bone index to assign

	for (;;)
	{
		tok.GetToken(t, sizeof(t));
		if (t[0] == '\0') break;				// EOF

		if (!_strnicmp(t, "bone", 4))
		{
			char name[256];
			tok.GetToken(name, sizeof(name));	// bone name
			tok.MatchToken("{");

			if (next >= n) { Errorf("crSkeletonData::Load: more bones than NumBones (%d)", n); break; }

			int idx = next++;
			crBoneData &b = Bones[idx];
			b.Name = _strdup(name);
			b.Offset.Zero();
			b.Rotation.Zero();
			b.RestPosition.Zero();
			b.Child = 0;
			b.Next  = 0;
			b.Dofs  = 0;
			b.Index = idx;
			b.Parent = (depth > 0) ? parentStack[depth] : -1;
			b.MirrorIndex  = idx;				// self until a mirror pass exists
			b.MirrorOffset = 0;

			if (depth + 1 < 128) parentStack[++depth] = idx;
		}
		else if (!_stricmp(t, "offset"))
		{
			if (depth > 0) tok.GetVector(Bones[parentStack[depth]].Offset);
			else { Vector3 tmp; tok.GetVector(tmp); }
		}
		else if (!_stricmp(t, "}"))
		{
			if (depth > 0) depth--;
		}
		else
		{
			int bit = sChannelBit(t);
			if (bit >= 0 && depth > 0)
			{
				// Channel present.  Optional "limit lo hi" or "lock" follows.
				bool locked = false;
				if (tok.CheckToken("lock", true))
					locked = true;				// present but not animated -> no DOF
				else if (tok.CheckToken("limit", true))
				{
					tok.GetFloat();				// lo
					tok.GetFloat();				// hi (limits unused for now)
				}
				if (!locked)
					Bones[parentStack[depth]].Dofs |= (u32)bit; // sChannelBit returns the DOF flag, not an index
			}
			// else: header leftovers (":", version int) or unknown -> ignore
		}
	}

	// --- Sibling/child linkage from parent indices (children in file order). ---
	for (int i = n - 1; i >= 0; i--)
	{
		int p = Bones[i].Parent;
		if (p >= 0 && p < n)
		{
			Bones[i].Next  = Bones[p].Child;	// prepend; reverse walk keeps order
			Bones[p].Child = &Bones[i];
			Bones[i].ParentPtr = &Bones[p];
		}
		else
		{
			Bones[i].ParentPtr = 0;
		}
	}

	// --- Legacy .skel fallback: files like edi/eli/krk/shn declare bones and
	// offsets only, with NO per-channel rotX/transX keywords. Their .anim
	// files still carry the full layout (root transXYZ + 3 rots per bone), so
	// a zero-DOF skeleton makes the DOF-derived blend/pose ranges disagree
	// with the frame buffers (out-of-bounds memcpy in crAnimFrame::Blend —
	// the all_chars crash). Default every bone to rotXYZ and give the root
	// transXYZ, matching the anim layout (same fallback the Oni2Rebuilt Rust
	// loader uses). ---
	{
		u32 anyDofs = 0;
		for (int i = 0; i < n; i++)
			anyDofs |= Bones[i].Dofs;
		if (anyDofs == 0)
		{
			Displayf("crSkeletonData::Load: '%s' declares no channels — legacy format, defaulting rotXYZ per bone + root transXYZ", filename);
			for (int i = 0; i < n; i++)
				Bones[i].Dofs = crBoneData::ROTATE_X | crBoneData::ROTATE_Y | crBoneData::ROTATE_Z;
			// The legacy full anim layout carries root transXYZ for root motion
			// and knockdowns.
			if (n > 0)
			{
				Bones[0].Dofs |= crBoneData::TRANSLATE_X | crBoneData::TRANSLATE_Y | crBoneData::TRANSLATE_Z;
			}
		}
		// Skeletons that DO declare channels are taken literally: creature
		// skels (tim/sci/kno) declare root transXYZ themselves, and prop skels
		// like IAControlDoor (root undeclared, two transZ joints = a sliding
		// double door) mean exactly what they say — forcing root translation
		// here made their DOF count disagree with their 2-channel anims.
	}

	// --- DOF totals summed over bones. ---
	NumRotDOFs = NumTransDOFs = 0;
	for (int i = 0; i < n; i++)
	{
		NumRotDOFs   += Bones[i].GetNumRotChannels();
		NumTransDOFs += Bones[i].GetNumTransChannels();
	}
	NumDOFs = NumRotDOFs + NumTransDOFs;
	Simple  = (NumTransDOFs == 0);

	// --- Mirror table: pair left/right bones so crAnimFrame::Mirror() can swap
	// their channels when playing a mirrored animation.  Bones are named with a
	// trailing "_l"/"_r"; a bone with no such suffix (root, spine, head, ...) is
	// symmetric and keeps MirrorIndex=self / MirrorOffset=0, which
	// crAnimFrame::Mirror() treats as an in-place negate.  Without this pass
	// every bone mirrored onto itself, so mirrored animations produced wrong
	// poses.  MirrorOffset is the signed distance, in DOF channels, from a bone's
	// channel block to its partner's (matches how Mirror() indexes Data[]).
	int numMirrored = sInitMirror(n, Bones);

	if (closeStream)
		s->Close();

	Displayf("crSkeletonData::Load: '%s' -> %d bones, %d DOFs (%d rot, %d trans), %d mirrored",
		filename ? filename : "(stream)", NumBones, NumDOFs, NumRotDOFs, NumTransDOFs, numMirrored);
	return true;
}

#include "data/rscimage.h"

bool crSkeletonData::LoadFromResource(const datResourceImage &image, u32 rootBoneAddr)
{
	if (!image.IsValidAddress(rootBoneAddr, 0x44))
		return false;

	u32 base = image.GetBase();
	const u8 *payload = image.GetPayload();

	int count = 0;
	while (image.IsValidAddress(rootBoneAddr + (u32)count * 0x44, 0x44))
	{
		u32 bOff = rootBoneAddr + (u32)count * 0x44 - base;
		u32 idx = *(const u32*)(payload + bOff + 0x38) & 0xffff;
		if (idx != (u32)count)
			break;
		count++;
	}

	if (count <= 0)
		return false;

	if (Bones)
		delete[] Bones;

	NumBones = count;
	Bones = new crBoneData[count];
	NumDOFs = 0;
	NumRotDOFs = 0;
	NumTransDOFs = 0;
	Simple = true;

	for (int i = 0; i < count; i++)
	{
		u32 bOff = rootBoneAddr + (u32)i * 0x44 - base;
		Bones[i].Index = i;
		Bones[i].RestPosition = *(const Vector3*)(payload + bOff);          // +0x00 absolute
		Bones[i].Offset = *(const Vector3*)(payload + bOff + 0x20);         // +0x20 parent-relative
		Bones[i].Rotation = *(const Vector3*)(payload + bOff + 0x2c);       // +0x2c rest rotation

		u32 namePtr = *(const u32*)(payload + bOff + 0x0c);
		if (namePtr && image.IsValidAddress(namePtr, 1))
		{
			Bones[i].Name = (const char*)(payload + (namePtr - base));
		}
		else
		{
			Bones[i].Name = "unnamed";
		}

		Bones[i].Dofs = crBoneData::ResolveResourceDofs(*(const u16*)(payload + bOff + 0x10),
		                                                 payload[bOff + 0x3d], payload[bOff + 0x3e]);
		Bones[i].Flags = payload[bOff + 0x3f];

		u32 parentPtr = *(const u32*)(payload + bOff + 0x1c);
		if (parentPtr >= rootBoneAddr && parentPtr < rootBoneAddr + (u32)count * 0x44)
		{
			int pIdx = (int)((parentPtr - rootBoneAddr) / 0x44);
			Bones[i].Parent = pIdx;
			Bones[i].ParentPtr = &Bones[pIdx];
		}
		else
		{
			Bones[i].Parent = -1;
			Bones[i].ParentPtr = nullptr;
		}

		u32 childPtr = *(const u32*)(payload + bOff + 0x18);
		if (childPtr >= rootBoneAddr && childPtr < rootBoneAddr + (u32)count * 0x44)
		{
			int cIdx = (int)((childPtr - rootBoneAddr) / 0x44);
			Bones[i].Child = &Bones[cIdx];
		}
		else
		{
			Bones[i].Child = nullptr;
		}

		u32 nextPtr = *(const u32*)(payload + bOff + 0x14);
		if (nextPtr >= rootBoneAddr && nextPtr < rootBoneAddr + (u32)count * 0x44)
		{
			int nIdx = (int)((nextPtr - rootBoneAddr) / 0x44);
			Bones[i].Next = &Bones[nIdx];
		}
		else
		{
			Bones[i].Next = nullptr;
		}

		Bones[i].MirrorIndex = i;
		Bones[i].MirrorOffset = 0;

		NumDOFs += Bones[i].GetNumRotChannels() + Bones[i].GetNumTransChannels();
		NumRotDOFs += Bones[i].GetNumRotChannels();
		NumTransDOFs += Bones[i].GetNumTransChannels();
		if (Bones[i].GetDofs() != crBoneData::DEFAULT_DOFS)
			Simple = false;
	}

	Displayf("crSkeletonData::LoadFromResource: %d bones loaded from pack (root '%s')", count, Bones[0].Name);
	return true;
}

u16 crBoneData::ResolveResourceDofs(u16 mask, u8 numRotChannels, u8 numTransChannels)
{
	const u16 kRotBits[3] = {ROTATE_X, ROTATE_Y, ROTATE_Z};
	const u16 kTransBits[3] = {TRANSLATE_X, TRANSLATE_Y, TRANSLATE_Z};
	if (numRotChannels > 3 || numTransChannels > 3)
	{
		// counts are not counts: keep the mask if it is plausible, else the default
		int r = crCountBits(mask & (ROTATE_X | ROTATE_Y | ROTATE_Z));
		int t = crCountBits(mask & (TRANSLATE_X | TRANSLATE_Y | TRANSLATE_Z));
		return (r + t > 0) ? mask : (u16)DEFAULT_DOFS;
	}
	if (crCountBits(mask & (ROTATE_X | ROTATE_Y | ROTATE_Z)) == numRotChannels &&
	    crCountBits(mask & (TRANSLATE_X | TRANSLATE_Y | TRANSLATE_Z)) == numTransChannels)
		return mask;
	u16 m = 0;
	for (int i = 0; i < numRotChannels; i++) m |= kRotBits[i];
	for (int i = 0; i < numTransChannels; i++) m |= kTransBits[i];
	if (mask & USE_LOCKED_ROT) m |= USE_LOCKED_ROT;
	if (mask & USE_LOCKED_SCALE) m |= USE_LOCKED_SCALE;
	return m;
}

#include "crskeleton/bone.h"

void crBoneData::Transform(const Matrix34 *parentGlobal, crBone *boneArray) const
{
	boneArray[Index].GlobalMtx.Dot(boneArray[Index].LocalMtx, *parentGlobal);
	for (const crBoneData *child = Child; child; child = child->Next)
	{
		child->Transform(&boneArray[Index].GlobalMtx, boneArray);
	}
}

#include "data/resource.h"

crBoneData::crBoneData(datResource &rsc)
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
{
#if defined(__WIN32PC)
	u32 start = rsc.Tell();

	// +0x00: Vector3 absolute rest position (exporter cache)
	rsc.GetVector3(RestPosition);

	// +0x0c: const char *Name
	Name = rsc.GetStringPtr();

	// +0x10: u16 Dofs
	Dofs = rsc.GetU16();
	rsc.Skip(0x2); // pad to +0x14

	// +0x14: Next, +0x18: Child, +0x1c: Parent (tagged image addresses; the
	// crSkeletonData page-in turns them into pointers and parent indices)
	rsc.PointerFixup((void *&)Next);
	rsc.PointerFixup((void *&)Child);
	rsc.PointerFixup((void *&)ParentPtr);

	// +0x20: Vector3 rest offset from the parent
	rsc.GetVector3(Offset);
	// +0x2c: Vector3 rest rotation (XYZ eulers)
	rsc.GetVector3(Rotation);

	// +0x38: u16 Index
	Index = rsc.GetU16();
	rsc.Skip(0x2); // pad to +0x3c

	// +0x3c: mirror index (0xff = none), +0x3d / +0x3e: rotate / translate channel
	// counts, +0x3f: flags.  The counts settle the dof mask (see ResolveResourceDofs).
	// +0x3d is the TRANSLATE channel count and +0x3e the ROTATE count, not the
	// other way round.  Vehicle bones carry 3/3 and so never showed the
	// difference; the rider rig does - its bones read 0/3 and its root 3/3,
	// which with this order totals 102 rotation channels over 34 bones plus 3
	// root translation channels.  That is exactly the 105 channels its
	// animations carry, and the 102 its skeleton header declares.  Read the
	// other way round every bone came out as translate-only, so the pose wrote
	// animation values into the bone positions and the whole rig collapsed
	// onto its root.
	MirrorIndex = rsc.GetU8();
	u8 numTransChannels = rsc.GetU8();
	u8 numRotChannels = rsc.GetU8();
	Flags = rsc.GetU8();
	Pad = 0;
	MirrorOffset = 0;
	Dofs = ResolveResourceDofs(Dofs, numRotChannels, numTransChannels);

	// +0x40: Mirror
	rsc.PointerFixup((void *&)Mirror);

	u32 consumed = rsc.Tell() - start;
	if (consumed < 0x44) {
		rsc.Skip(0x44 - consumed);
	}
#else
	rsc.PointerFixup((void *&)Name);
	rsc.PointerFixup((void *&)Next);
	rsc.PointerFixup((void *&)Child);
	rsc.PointerFixup((void *&)ParentPtr);
	rsc.PointerFixup((void *&)Mirror);
#endif
}

crSkeletonData::crSkeletonData(datResource &rsc)
	: NumBones(0)
	, NumRotDOFs(0)
	, NumTransDOFs(0)
	, NumDOFs(0)
	, Simple(0)
	, Bones(nullptr)
	, m_RefCount(0)
{
#if defined(__WIN32PC)
	u32 start = rsc.Tell();

	// +0x0: u16 NumBones
	NumBones = rsc.GetU16();
	// +0x2: u8 NumRotDOFs
	NumRotDOFs = rsc.GetU8();
	// +0x3: u8 NumTransDOFs
	NumTransDOFs = rsc.GetU8();
	// +0x4: u16 NumDOFs
	NumDOFs = rsc.GetU16();
	// +0x6: u16 Simple
	Simple = rsc.GetU16();
	// +0x8: ptr -> crBoneData[NumBones]
	u32 ptrAt = rsc.Tell();
	u32 bonesAddr = rsc.GetPtr();
	rsc.Seek(ptrAt);
	ObjectFixup(rsc, Bones, (int)NumBones);

	// The bones' Next/Child/Parent/Mirror still hold tagged image addresses:
	// turn them into pointers into the array and derive the runtime parent index
	// (the console did this through crSkeleton::Init reading the parent's Index).
	if (Bones && bonesAddr) {
		const u32 kBoneSize = 0x44;
		auto resolve = [&](crBoneData *&p) {
			u32 addr = rsc.AddressOf(p);
			p = nullptr;
			if (addr >= bonesAddr && addr < bonesAddr + (u32)NumBones * kBoneSize && (addr - bonesAddr) % kBoneSize == 0)
				p = &Bones[(addr - bonesAddr) / kBoneSize];
		};
		for (int i = 0; i < (int)NumBones; i++) {
			resolve(Bones[i].Next);
			resolve(Bones[i].Child);
			resolve(Bones[i].ParentPtr);
			resolve(Bones[i].Mirror);
			Bones[i].Parent = Bones[i].ParentPtr ? (int)(Bones[i].ParentPtr - Bones) : -1;
			if (Bones[i].Index != (u16)i) Bones[i].Index = (u16)i;
		}
		NumDOFs = NumRotDOFs = NumTransDOFs = 0;
		Simple = true;
		for (int i = 0; i < (int)NumBones; i++) {
			NumDOFs += (u16)(Bones[i].GetNumRotChannels() + Bones[i].GetNumTransChannels());
			NumRotDOFs += (u8)Bones[i].GetNumRotChannels();
			NumTransDOFs += (u8)Bones[i].GetNumTransChannels();
			if (Bones[i].GetDofs() != crBoneData::DEFAULT_DOFS) Simple = false;
		}
	}

	u32 consumed = rsc.Tell() - start;
	if (consumed < 0xc) {
		rsc.Skip(0xc - consumed);
	}
#else
	rsc.PointerFixup((void *&)Bones);
	if (Bones) {
		for (int i = 0; i < (int)NumBones; ++i) {
			ObjectFixup(rsc, Bones[i]);
		}
	}
#endif
}

// Walk the tree the way Save writes it and record the order the bones come
// out in.  The loader numbers bones in the order it meets them, so a file
// whose depth-first order is not 0,1,2,... comes back with a different
// numbering than the skeleton it was written from - and every .anim exported
// beside it, whose channels are laid out in bone order, would then drive the
// wrong bones.
static void sCollectBoneOrder(const crBoneData *b, atArray<int> &order)
{
	order.Append(b->GetIndex());
	for (const crBoneData *c = b->Child; c; c = c->Next)
		sCollectBoneOrder(c, order);
}

static void sWriteBoneAscii(datAsciiTokenizer &tok, const crBoneData *b)
{
	tok.StartLine();
	tok.Put("bone ");
	tok.Put(b->GetName() ? b->GetName() : "unnamed");
	tok.Put(" ");
	tok.StartBlock();

	tok.StartLine();
	tok.Put("offset ");
	tok.Put(b->Offset);
	tok.EndLine();

	if (b->Dofs & crBoneData::TRANSLATE_X) {
		tok.StartLine();
		tok.Put("transX");
		tok.EndLine();
	}
	if (b->Dofs & crBoneData::TRANSLATE_Y) {
		tok.StartLine();
		tok.Put("transY");
		tok.EndLine();
	}
	if (b->Dofs & crBoneData::TRANSLATE_Z) {
		tok.StartLine();
		tok.Put("transZ");
		tok.EndLine();
	}
	if (b->Dofs & crBoneData::ROTATE_X) {
		tok.StartLine();
		tok.Put("rotX");
		tok.EndLine();
	}
	if (b->Dofs & crBoneData::ROTATE_Y) {
		tok.StartLine();
		tok.Put("rotY");
		tok.EndLine();
	}
	if (b->Dofs & crBoneData::ROTATE_Z) {
		tok.StartLine();
		tok.Put("rotZ");
		tok.EndLine();
	}

	for (const crBoneData *c = b->Child; c; c = c->Next)
	{
		sWriteBoneAscii(tok, c);
	}

	tok.EndBlock();
}

bool crSkeletonData::Save(Stream *s) const
{
	if (!s || NumBones == 0 || !Bones)
		return false;

	// Reversibility check, before a byte is written.
	{
		atArray<int> order;
		for (int i = 0; i < (int)NumBones; i++)
			if (Bones[i].Parent < 0 && Bones[i].ParentPtr == nullptr)
				sCollectBoneOrder(&Bones[i], order);

		if (order.GetCount() != (int)NumBones)
			Quitf("crSkeletonData::Save: the bone tree reaches %d of %d bones - a bone is "
			      "orphaned or the child links are cyclic, and the file would be short",
			      order.GetCount(), (int)NumBones);

		for (int i = 0; i < order.GetCount(); i++)
			if (order[i] != i)
				Quitf("crSkeletonData::Save: bone %d ('%s') comes %d'th in depth-first "
				      "order, so reloading this file would renumber the skeleton and misalign every "
				      "animation exported beside it", order[i], Bones[order[i]].GetName(), i);
	}

	datAsciiTokenizer tok;
	tok.Init("skel", s);

	tok.Put("Version: 101\n");
	tok.Put("NumBones ");
	tok.Put((int)NumBones);
	tok.Put("\n\n");

	for (int i = 0; i < (int)NumBones; i++)
	{
		if (Bones[i].Parent < 0 && Bones[i].ParentPtr == nullptr)
		{
			sWriteBoneAscii(tok, &Bones[i]);
		}
	}
	return true;
}

bool crSkeletonData::Save(const char *filename) const
{
	if (!filename || !*filename || NumBones == 0 || !Bones)
		return false;

	Stream *s = ASSET.Create(filename, "skel");
	if (!s) {
		Errorf("crSkeletonData::Save: failed to open '%s' for writing", filename);
		return false;
	}

	bool ok = Save(s);
	s->Close();
	if (ok)
		Displayf("crSkeletonData::Save: wrote '%s' (%d bones)", filename, (int)NumBones);
	return ok;
}
