#include "CreatureType.h"
#include "rmcore\drawable.h"
#include "crskeleton/skeleton.h"

mcCreatureType::~mcCreatureType()
{
	Release();
}

void mcCreatureType::Release()
{
	if(m_poRmcDrawable) 
	{
		 delete m_poRmcDrawable;
		m_poRmcDrawable = NULL;
	}

	if(m_poSkeleton) 
	{
		delete m_poSkeleton;
		m_poSkeleton = NULL; 
	}
	m_poCreatureAnimGroup = NULL;
}

void mcCreatureType::Init()
{
	m_poRmcDrawable = NULL;
	m_poSkeleton	= NULL;
	m_poCreatureAnimGroup = NULL;
	m_szResourceAnimGroupName[0] = 0;
}

void mcCreatureType::LoadTypeData		(const char *pName, const mcCreatureAnimGroup *poCreatureAnimGroup, const char *pSexName)
{
	Load(pName);
	m_poCreatureAnimGroup = poCreatureAnimGroup;

	if(pSexName)
	{
		if(stricmp("m", pSexName) == 0)
		{
			m_nGenderType = mcCreatureType::kMalePed;
		}
		else if(stricmp("f", pSexName) == 0)
		{
			m_nGenderType = mcCreatureType::kFemalePed;
		}
		else
		{
			Warningf("Unsupported sex type for peds!");
		}

	}
}
		
void mcCreatureType::Load(const char *szFileName)
{
	Assert(m_poRmcDrawable == NULL && " m_poRmcDrawable has not yet been released");
	m_poRmcDrawable = new rmcDrawable;
	char szFolder[30];
	formatf(szFolder, sizeof(szFolder), "$/ped/%s",szFileName);
	ASSET.PushFolder(szFolder);
	{
		const bool cbLoadDrawable = m_poRmcDrawable->Load(szFileName, NULL);
		if(!cbLoadDrawable)
		{
			Quitf("Could not load creature rmcDrawable, %s",szFileName);
		}
	}
	ASSET.PopFolder();
	
	crSkeletonData *poSkeldata = m_poRmcDrawable->GetSkeletonData();
	Assert(poSkeldata != NULL && "The Creature has to have a Skeleton Data");

 	const int cnNumBones = poSkeldata->GetNumBones();
	m_poSkeleton = new crSkeleton(cnNumBones);
	m_poSkeleton->Init(*poSkeldata,NULL);
	m_poHeadBoneData = m_poSkeleton->GetSkeletonData().FindBone("head");
	Assert(m_poHeadBoneData && " Head bone not found in skeleton");

	SetName(szFileName);
	Reset();

}

// PC port: read a creature type back out of a resource pack.
//
// Decoded from the October 2004 alpha build, which still carries symbols:
// mcCreatureType::mcCreatureType(datResource &) at 0x42cae8 chains to
// mcCullableType's resource constructor, writes its vtable at +0x28, then
// relocates a pointer at +0x30, constructs an rmcDrawable at +0x34 and a
// crSkeleton at +0x38, and relocates two more pointers at +0x3c and +0x40.
// mcCreatureType::Init at 0x42c518 zeroes +0x2c through +0x40 plus +0x48 and
// +0x4c, so the alpha's class carries members this source does not declare.
//
// That leaves one ambiguity worth being careful about: the slot at +0x30 has no
// counterpart among the declared members, so an image written by a build
// WITHOUT it would put the drawable four bytes earlier.  Rather than pick one
// and silently misread every field after it, the reader looks at both
// candidate arrangements and takes the one whose drawable and skeleton slots
// actually hold addresses inside the image.
// mcCreatureAnimGroup in the image: an atArray (pointer, u16 count, u16
// capacity) and then the group's name.
static const u32 kAnimGroupNameOffset = 0x08;

static bool sPlausiblePair(const datResourceImage *image, u32 a, u32 b)
{
	if (!image)
		return false;
	if (a && !image->IsValidAddress(a, 4))
		return false;
	if (b && !image->IsValidAddress(b, 4))
		return false;
	return (a != 0) || (b != 0);
}

mcCreatureType::mcCreatureType(datResource &rsc)
	: mcCullableType(rsc)
{
	Init();
	m_poHeadBoneData = NULL;
	m_bRaceLayerType = false;
	m_nGenderType = kMalePed;

	const datResourceImage *image = rsc.GetImage();

	rsc.GetVTable();
	rsc.Skip(4);

	u32 probe = rsc.Tell();
	datResource peek = rsc.At(probe);
	u32 w0 = peek.GetPtr();
	u32 w1 = peek.GetPtr();
	u32 w2 = peek.GetPtr();

	static int s_logged = 0;
	bool withReserved = sPlausiblePair(image, w1, w2);
	if (!withReserved && sPlausiblePair(image, w0, w1)) {
		if (!s_logged++)
			Displayf("mcCreatureType: resource layout has no reserved slot before the drawable");
	} else {
		if (!s_logged++)
			Displayf("mcCreatureType: resource layout carries the reserved slot at +0x30");
		void *reserved = NULL;
		rsc.PointerFixup(reserved);
	}

	ObjectFixup(rsc, m_poRmcDrawable);
	ObjectFixup(rsc, m_poSkeleton);

	// The last two slots are a bone inside the skeleton data and the shared
	// animation group.  The console relocated both and was done, because both
	// were live memory by then.  Neither is here: the skeleton was rebuilt
	// above, so a pointer into the image's copy of it points at nothing this
	// build owns, and the group's clips are still console data.  So read the
	// addresses and then resolve them.
	rsc.PointerFixup(m_poHeadBoneData);
	rsc.PointerFixup(m_poCreatureAnimGroup);

	// The head bone drives the ped's look-at (mcCreatureIK::Init takes its
	// index).  Find it by name in the skeleton that was actually built rather
	// than trying to map the image address onto it.
	m_poHeadBoneData = NULL;
	if (m_poSkeleton && m_poSkeleton->HasSkeletonData()) {
		const crSkeletonData &skelData = m_poSkeleton->GetSkeletonData();
		m_poHeadBoneData = skelData.FindBone("head");
		if (!m_poHeadBoneData && skelData.GetNumBones() > 0) {
			Warningf("mcCreatureType: no bone named 'head' in '%s'; head tracking will aim from the root",
			         GetName() ? GetName() : "(unnamed)");
			m_poHeadBoneData = skelData.GetBone(0);
		}
	}

	// The pack's animation group is real, but every clip hanging off it is
	// console data this build cannot play, and the identical clips are already
	// on disc - the anim manager loads them from tune/ped/<city>/<group>.cal
	// before the types are read.  So keep only the group's name (it sits after
	// the array, where mcCreatureAnimGroup declares it) and let
	// mcCreatureTypeManager link the type to the group that was really built.
	m_szResourceAnimGroupName[0] = 0;
	const u32 groupAddr = rsc.AddressOf(m_poCreatureAnimGroup);
	m_poCreatureAnimGroup = NULL;
	if (groupAddr && image && image->IsValidAddress(groupAddr + kAnimGroupNameOffset, 1)) {
		formatf(m_szResourceAnimGroupName, sizeof(m_szResourceAnimGroupName), "%s",
		        image->ReadString(groupAddr + kAnimGroupNameOffset));
	}

	// The group name also says which the ped is: the city packs name their
	// groups after the model, "mped01" and "fped01".
	if (strstr(m_szResourceAnimGroupName, "fped"))
		m_nGenderType = kFemalePed;
}
