#include "text/language.h"

#include <string.h>

const char *txtLanguage::mNames[txtLanguage::kCount] = {
	"english", "french", "german", "spanish", "italian", "japanese"
};

const char *txtLanguage::mAbbreviations[txtLanguage::kCount] = {
	"en", "fr", "de", "es", "it", "jp"
};

const char *txtLanguage::Get(int language)
{
	if (!IsValid(language)) return "";
	return mNames[language];
}

const char *txtLanguage::GetAbbreviation(int language)
{
	if (!IsValid(language)) return "";
	return mAbbreviations[language];
}

int txtLanguage::Get(const char *name)
{
	if (!name) return kEn;
	for (int i = 0; i < kCount; i++) {
		if (!_stricmp(name, mNames[i]) || !_stricmp(name, mAbbreviations[i])) return i;
	}
	return kEn;
}
