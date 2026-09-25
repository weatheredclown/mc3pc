
#include "globaloptions.h"

mcGlobalGameOptions::mcGlobalGameOptions()
	: m_IsDirty(false), m_AudioConfigOptionsChanged(false)
{
	SetToDefaults();
}

void mcGlobalGameOptions::SetToDefaults()
{
	SetAudioConfigOption("Miles Fast 2D Positional Audio");

	SetScreenWidthOption		(640);
	SetScreenHeightOption		(480);
	SetScreenDepthOption		(32);
	SetDrawDistanceOption		(500);
	SetEnvironmentMappingOption	(true);
	SetReflectionsOption		(true);
	SetShadowsOption			(true);
	SetFullScreenEffectsOption	(true);

	m_Language = txtLanguage::kEn;

	ProcessArgs();
}

void mcGlobalGameOptions::ProcessArgs()
{
}

void mcGlobalGameOptions::SetAudioConfigOption(const char * cpSetting)
{ 
	strncpy(m_AudioConfig, cpSetting, sizeof(m_AudioConfig));
	m_AudioConfig[sizeof(m_AudioConfig)-1]=0;

	SetAudioConfigOptionChanged(true);
	SetHasDataChanged(true);
}

void	mcGlobalGameOptions::SetScreenWidthOption	(unsigned int Width)
{
	if	(m_ScreenWidth != Width)
	{
		m_ScreenWidth = Width;
		SetHasDataChanged(true);
	}
}

void	mcGlobalGameOptions::SetScreenHeightOption	(unsigned int Height)
{
	if	(m_ScreenHeight != Height)
	{
		m_ScreenHeight = Height;
		SetHasDataChanged(true);
	}
}

void	mcGlobalGameOptions::SetScreenDepthOption	(unsigned int Depth)
{
	if	(m_ScreenDepth != Depth)
	{
		m_ScreenDepth = Depth;
		SetHasDataChanged(true);
	}
}

void	mcGlobalGameOptions::SetDrawDistanceOption	(unsigned int Distance)
{
	if	(m_DrawDistance != Distance)
	{
		m_DrawDistance = Distance;
		SetHasDataChanged(true);
	}
}

void	mcGlobalGameOptions::SetEnvironmentMappingOption	(unsigned int EnvMap)
{
	if	(m_EnvMapping != EnvMap)
	{
		m_EnvMapping = EnvMap;
		SetHasDataChanged(true);
	}
}

unsigned	int	mcGlobalGameOptions::GetEnvironmentMappingOption	(void)
{
	return(m_EnvMapping);
}

void	mcGlobalGameOptions::SetReflectionsOption	(unsigned int Reflections)
{
	if	(m_Reflections != Reflections)
	{
		m_Reflections = Reflections;
		SetHasDataChanged(true);
	}
}

void	mcGlobalGameOptions::SetShadowsOption	(unsigned int Shadows)
{
	if	(m_Shadows != Shadows)
	{
		m_Shadows = Shadows;
		SetHasDataChanged(true);
	}
}

void	mcGlobalGameOptions::SetFullScreenEffectsOption	(unsigned int FullScreenEffects)
{
	if	(m_FullScreenEffects != FullScreenEffects)
	{
		m_FullScreenEffects = FullScreenEffects;
		SetHasDataChanged(true);
	}
}

void mcGlobalGameOptions::SetHasDataChanged(bool didIt)
{ 
	m_IsDirty = didIt; 
}
