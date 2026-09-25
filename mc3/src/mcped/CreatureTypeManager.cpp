#include "CreatureTypeManager.h"
#include "CreatureType.h"
#include "CreatureAnimMgr.h"
#include	"mccullable/cullable.h"
#include	"mccullable/cullabletype.h"
#include	"mccullable/cullableclass.h"
#include "creatureType.h"
#include "mcped/CreatureNavigator.h"
#include "rmcore/light.h"
class Matrix34;
#include "mcped/CreatureThreat.h"
#include "data/base.h"
class  mcCreatureType;
#include "data/token.h"
#include "data/assetcfg.h"
#include "core/output.h"


#include "data/assetcfg.h"

mcCreatureTypeManager::mcCreatureTypeManager()
{
	Init();
}

mcCreatureTypeManager::~mcCreatureTypeManager()
{
	Release();

	Assert(m_oRaceStarterTypeArr.GetCount() == 0 && "Did not shut down race starter data properly");
}

void mcCreatureTypeManager::Init()
{
}

void mcCreatureTypeManager::Release()
{
	for(int nElement = 0; nElement < m_oCreatTypeArr.GetCount(); nElement++)
	{
		delete m_oCreatTypeArr[nElement];
		m_oCreatTypeArr[nElement] = NULL;
	}
	m_oCreatTypeArr.Reset();
}

#include	"data/resource.h"
#include	"data/rscimage.h"
#include	"core/output.h"

// PC port: the root object of a <city>_peds.pck.
//
// Decoded from the alpha build's
// mcCreatureTypeManager::mcCreatureTypeManager(datResource &) at 0x42d2c0.  It
// relocates two arrays and walks both, constructing an mcCreatureType for every
// non-null entry:
//
//     +0x00  mcCreatureType **   +0x04  u16 count      (m_oCreatTypeArr)
//     +0x08  mcCreatureType **   +0x0c  u16 count      (m_oRaceStarterTypeArr)
//     +0x10  vtable
//
// Pointer plus a 16-bit count is an atArray, and the vtable landing after the
// members is this compiler putting it after the introducing class's fields.

static void sReadTypeArray(datResource &rsc, atArray<mcCreatureType *> &out, const char *what)
{
	u32 addr = rsc.GetPtr();
	int count = (int)rsc.GetU16();
	rsc.GetU16();

	out.Reset();
	if (!addr || count <= 0 || !rsc.GetImage())
		return;

	out.Resize(count);
	datResource sub = rsc.At(addr);
	int built = 0;
	for (int i = 0; i < count; i++) {
		mcCreatureType *type = NULL;
		sub.PointerFixup(type);
		sub.Construct(type);
		out[i] = type;
		if (type)
			built++;
	}
	Displayf("mcCreatureTypeManager: %s - %d of %d entries built", what, built, count);
}

void mcCreatureTypeManager::ReadFromResource(datResource &rsc)
{
	sReadTypeArray(rsc, m_oCreatTypeArr, "creature types");
	sReadTypeArray(rsc, m_oRaceStarterTypeArr, "race starter types");
	rsc.GetVTable();
}

// mcCreatureAnimMgr::FindAnimGroupByName asserts when it misses, and walks the
// race array with the city array's count on the way there, so the lookup is
// done here instead: a group this pack names but the .cal files do not have is
// something to report and carry on from.
static const mcCreatureAnimGroup *sFindAnimGroup(const mcCreatureAnimMgr &rfAnimMgr, const char *szName)
{
	if (!szName || !*szName)
		return NULL;
	for (int nGroup = 0; nGroup < rfAnimMgr.GetNumAnimationsGroup(); nGroup++) {
		const mcCreatureAnimGroup &rfGroup = rfAnimMgr.GetCreatureAnimGroup(nGroup);
		if (0 == stricmp(rfGroup.GetAnimGroupName(), szName))
			return &rfGroup;
	}
	return NULL;
}

bool mcCreatureTypeManager::LoadCityPedResource(const char *szCityName, const mcCreatureAnimMgr *poCreatureAnimMgr)
{
	if (!szCityName || !*szCityName)
		return false;

	static datResourceImage s_PedImage;
	static char s_LoadedCity[64] = "";

	if (!stricmp(s_LoadedCity, szCityName) && m_oCreatTypeArr.GetCount() > 0)
		return true;

	char path[128];
	formatf(path, sizeof(path), "resources/city/%s_peds", szCityName);
	if (!s_PedImage.Load(path)) {
		Warningf("mcCreatureTypeManager: no ped resource '%s.pck'", path);
		return false;
	}

	const u32 kManagerPointerOffset = 0x78;
	u32 slot = s_PedImage.GetBase() + kManagerPointerOffset;
	if (!s_PedImage.IsValidAddress(slot, 4)) {
		Warningf("mcCreatureTypeManager: '%s.pck' is too small to hold a root", path);
		return false;
	}

	const u8 *at = s_PedImage.At(slot);
	u32 mgrAddr = (u32)at[0] | ((u32)at[1] << 8) | ((u32)at[2] << 16) | ((u32)at[3] << 24);
	if (!mgrAddr || !s_PedImage.IsValidAddress(mgrAddr, 0x10)) {
		Warningf("mcCreatureTypeManager: '%s.pck' has no manager at +0x%x (read %08x)",
		         path, kManagerPointerOffset, mgrAddr);
		return false;
	}

	datResource rsc(s_PedImage, mgrAddr);
	ReadFromResource(rsc);

	// Give every type the animation group it names.  A type that does not get
	// one is left alone rather than dropped: the populater already refuses to
	// spawn one (mcCreaturePopulater::UpdateCreaturePopulate), and a viewer
	// that brings its own clips has no manager to link against at all.
	int nLinked = 0;
	if (poCreatureAnimMgr) {
		for (int nType = 0; nType < m_oCreatTypeArr.GetCount(); nType++) {
			mcCreatureType *poType = m_oCreatTypeArr[nType];
			if (!poType)
				continue;
			const char *szGroup = poType->GetResourceAnimGroupName();
			const mcCreatureAnimGroup *poGroup = sFindAnimGroup(*poCreatureAnimMgr, szGroup);
			if (!poGroup) {
				Warningf("mcCreatureTypeManager: ped '%s' wants animation group '%s', which tune/ped/%s does not have - it will not be spawned",
				         poType->GetName() ? poType->GetName() : "(unnamed)",
				         (szGroup && *szGroup) ? szGroup : "(unnamed)", szCityName);
				continue;
			}
			poType->SetCreatureAnimGroup(poGroup);
			nLinked++;
		}
	}

	formatf(s_LoadedCity, sizeof(s_LoadedCity), "%s", szCityName);
	Displayf("mcCreatureTypeManager: loaded %d ped types from '%s.pck' (%d linked to an animation group)",
	         m_oCreatTypeArr.GetCount(), path, nLinked);
	return m_oCreatTypeArr.GetCount() > 0;
}
