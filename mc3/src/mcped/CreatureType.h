#ifndef	MCPED_CREATURE_TYPE_H
#define	MCPED_CREATURE_TYPE_H
class rmcDrawable;
class crSkeleton;
class crBoneData;

#include "mccullable/cullabletype.h"
#include "CreatureAnimMgr.h"
class datResource;

class mcCreatureType : public mcCullableType
{
	public:
		enum PedGenderType
		{
			kMalePed = 0,
			kFemalePed
		};
		mcCreatureType(const bool cbRaceLayerType = false);
		mcCreatureType(class datResource &rsc);
		// The animation group the pack says this type plays ("mped01",
		// "fped01").  The pack holds the group but its clips are console data,
		// and the loose ones the anim manager already loaded from
		// tune/ped/<city> are the same animations, so the type is linked to
		// that group by name rather than the packed one being decoded.
		const char *GetResourceAnimGroupName() const { return m_szResourceAnimGroupName; }
		void SetCreatureAnimGroup(const mcCreatureAnimGroup *poGroup) { m_poCreatureAnimGroup = poGroup; }
		virtual ~mcCreatureType();
		void Release();
		void Load(const char *szFullFileName);		
		virtual	void	LoadTypeData		(const char *pName, const mcCreatureAnimGroup *poCreatureAnimGroup, const char *pSexName = NULL);
		virtual	void	LoadTypeData		(const char *, const char *){ Assert(0 && "Use the other LoadTypeData");}
		
		rmcDrawable & GetRmDrawable()const		 { Assert(m_poRmcDrawable);	return *m_poRmcDrawable;}
		crSkeleton  & GetSkeleton()const		 { Assert(m_poSkeleton);	return *m_poSkeleton;}
		const mcCreatureAnimGroup &GetCreatureAnimGroup()const {Assert(m_poCreatureAnimGroup); return *m_poCreatureAnimGroup;}
	protected:
		void Init();
	protected:		
		rmcDrawable		 *m_poRmcDrawable;
		crSkeleton		 *m_poSkeleton;
		const crBoneData *m_poHeadBoneData;
		const mcCreatureAnimGroup *m_poCreatureAnimGroup;
		bool			  m_bRaceLayerType;
		PedGenderType	 m_nGenderType;
		char			  m_szResourceAnimGroupName[32];	// empty unless read from a pack
};
#endif