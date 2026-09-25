#ifndef TEXT_LANGUAGE_H
#define TEXT_LANGUAGE_H

////////////////////////////////////////
// text/language.h
//
// txtLanguage - the set of shipped languages, with name and abbreviation
// tables.  The game stores its selection as txtLanguage::eLanguage and uses
// the abbreviation to build per-language asset names.
////////////////////////////////////////

class txtLanguage
{
public:
	enum eLanguage {
		kEn = 0,
		kFr,
		kDe,
		kEs,
		kIt,
		kJp,
		kCount
	};

	// Full name of a language ("english"), or "" when out of range.
	static const char *Get(int language);
	// Language id for a name or abbreviation (case-insensitive); kEn if unknown.
	static int Get(const char *name);

	static const char *GetAbbreviation(int language);
	static bool IsValid(int language)			{ return language >= 0 && language < kCount; }

	static const char *mNames[kCount];
	static const char *mAbbreviations[kCount];
};

#endif // TEXT_LANGUAGE_H
