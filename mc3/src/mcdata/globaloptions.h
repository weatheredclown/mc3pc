
#ifndef MCDATA_GLOBALOPTIONS_H
#define MCDATA_GLOBALOPTIONS_H

#include "text/language.h"

class mcGlobalGameOptions
{
public:
						mcGlobalGameOptions();

	void				SetToDefaults();
	void				ProcessArgs();
	bool				Load();
	bool				Save();

	const char *		GetName() const												{ return "userdata\\options.cfg"; }

	void				SetAudioConfigOption(const char * cpSetting);
	void				SetAudioConfigOptionChanged(const bool cbChanged)			{ m_AudioConfigOptionsChanged = cbChanged; }

	void				SetScreenWidthOption		(unsigned int Width);
	void				SetScreenHeightOption		(unsigned int Height);
	void				SetScreenDepthOption		(unsigned int Depth);
	void				SetDrawDistanceOption		(unsigned int Distance);
	void				SetEnvironmentMappingOption	(unsigned int EnvMap);
	unsigned	int		GetEnvironmentMappingOption	(void);
	void				SetReflectionsOption		(unsigned int Reflections);
	void				SetShadowsOption			(unsigned int Shadows);
	void				SetFullScreenEffectsOption	(unsigned int FullScreenEffects);

protected:
	char							m_AudioConfig[64];

	unsigned	int					m_ScreenWidth;
	unsigned	int					m_ScreenHeight;
	unsigned	int					m_ScreenDepth;
	unsigned	int					m_DrawDistance;
	unsigned	int					m_EnvMapping;
	unsigned	int					m_Reflections;
	unsigned	int					m_Shadows;
	unsigned	int					m_FullScreenEffects;
	
	int								m_InputDevice;
	int								m_PadIndex;
	int								m_Language;

	bool							m_IsDirty;

	void				SetHasDataChanged(bool didIt);

private:
	bool				m_AudioConfigOptionsChanged;
};

#endif
