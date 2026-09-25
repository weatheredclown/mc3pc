#ifndef	MCPED_CREATURE_ANIM_H
#define	MCPED_CREATURE_ANIM_H


class crfAnimation;

class mcCreatureAnim
{
	public:
		enum TAnimType
		{
			kWalkAnim = 0,
			kRunAnim,
			kDiveAnim,
			kIdleAnim,
			kAvoidAlcovesAnim,
			kAlcoveIdleAnim,
			kAlcoveIdle2WalkAnim,
			kAvoidFenceAnim,
			kFenceIdleAnim,
			kFence2FallAnim,
			kPancakeAnim,
			kPancakeIdleAnim,
			kRaceStarterCountdown,
			kRaceStarterGo,
			kAvoidRun,
			kAvoidSpin,
			kAvoidHop,
			kAvoidJump,
			kAvoidLeap,
			kAvoidSlide,
			kCheer,
			kClap,
			kLook,
			kWow,
			kYell,
			kWaveoff,
			kAlert,

			kNumAnimTypes,
			kInvalidAnim,
		};
	protected:
		crfAnimation *m_poAnimation;
		TAnimType	m_eAnimType;
		bool		m_bHasTransforms;
		bool		m_bLoop;
		static int ms_MaxChannels;
	public:
		mcCreatureAnim();
		virtual ~mcCreatureAnim();
		void Reset();

};
#endif