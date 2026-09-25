#ifndef	MCPED_CREATURE_TYPE_MANAGER_H
#define	MCPED_CREATURE_TYPE_MANAGER_H
#include "atl\array.h"

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

class mcCreatureType;
class datAsciiTokenizer;
class datResource;

class mcCreatureTypeManager
{
	public:
		mcCreatureTypeManager(class datResource &rsc);
		void ReadFromResource(class datResource &rsc);
		// With an animation manager, each type is also linked to the group of
		// the same name that it already built from tune/ped/<city>; without
		// one the types come back with no animations, which is all a viewer
		// that plays its own clips needs.
		bool LoadCityPedResource(const char *szCityName, const mcCreatureAnimMgr *poCreatureAnimMgr = NULL);

		mcCreatureTypeManager();
		virtual ~mcCreatureTypeManager();
		void Release();
		void Load(const char *szFilePath, const char *szFileName, mcCreatureAnimMgr &rfCreatureAnimMgr);	
		const mcCreatureType *GetCreatureType(const int cnCreatureType)const { return m_oCreatTypeArr[cnCreatureType];}	
		
		int GetNumCreatureTypes()const { return m_oCreatTypeArr.GetCount();}
	public:
	protected:
		void Init();
	protected:		
		atArray<mcCreatureType*> m_oCreatTypeArr;
		atArray<mcCreatureType*> m_oRaceStarterTypeArr;
};

#endif