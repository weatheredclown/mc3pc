#ifndef	MCPED_CREATURE_ANIM_MGR_H
#define	MCPED_CREATURE_ANIM_MGR_H

#include "atl/array.h"
#include "CreatureAnim.h"

class mcCreatureAnimGroup
{
	public:
		const char *GetAnimGroupName()const { return m_szAnimGroupName;}
		void  Reset();

		friend class mcCreatureAnimGroupFactory;
	protected:
		void  Load(const int cnAnim, const enum mcCreatureAnim::TAnimType ceAnimType, const char *szAnimGroupName, const bool cbHasTransforms, const bool cbLoopAnim);
		void  Release();
	protected:
		atArray<mcCreatureAnim> m_oCreatureAnimArr;	
		char m_szAnimGroupName[32];
};

class mcCreatureAnimMgr
{
	public:
		const mcCreatureAnimGroup & GetCreatureAnimGroup(const int cnCreatureAnimGrp)const { return m_oCreatureAnimGroupArr[cnCreatureAnimGrp];}
		void  Reset();
		int   GetNumAnimationsGroup()const { return m_oCreatureAnimGroupArr.GetCount();}
		friend class mcCreatureAnimFactory;
	protected:
		void  Release();
		mcCreatureAnimGroup & GetCreatureAnimGroup(const int cnCreatureAnimGrp){ return m_oCreatureAnimGroupArr[cnCreatureAnimGrp];}
	protected:
		atArray<mcCreatureAnimGroup> m_oCreatureAnimGroupArr;
		atArray<mcCreatureAnimGroup> m_oCreatureRaceAnimGroupArr;
};
#endif