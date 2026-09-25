
#ifndef MCDATA_CONFIG_H
#define MCDATA_CONFIG_H

class mcGlobalGameOptions;

class mcConfig
{

public:

	static mcGlobalGameOptions &GetGlobalGameOptions()		{ return sGlobalGameOptions; }

	// The scene settings a race used to carry.  The game filled them in from the race it was
	// about to run; the viewer has no race, so it reads them off the pack name instead - city
	// packs are named <city>_<time of day>_<weather>.
	static bool IsDaytime()									{ return sDaytime; }
	static bool IsRainy()									{ return sRainy; }
	static void SetScene(bool daytime, bool rainy)			{ sDaytime = daytime; sRainy = rainy; }

private:

	static bool sDaytime;
	static bool sRainy;

	static mcGlobalGameOptions		sGlobalGameOptions;
};

#endif
