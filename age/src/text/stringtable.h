#ifndef TEXT_STRINGTABLE_H
#define TEXT_STRINGTABLE_H

#include "atl/wstring.h"   // W2A for callers formatting table strings
#include "gfx/font.h"
#include "data/hash.h"

class txtStringData {
public:
    gfxFont* GetFont() const;
    const wchar_t* GetString() const;
    
    const wchar_t* mString = nullptr;
    float m_ScaleX = 1.0f;
    float m_ScaleY = 1.0f;
    char m_FontName[32] = "";
    int m_OffsetX = 0;
    int m_OffsetY = 0;
    u16 m_Flags = 0;

private:
    friend class txtStringTable;
    mutable gfxFont m_Font;                       // debug fallback
    mutable class txtFontTex* m_FontTex = nullptr; // resolved from m_FontName
    mutable bool m_FontResolved = false;
};

#include <vector>

class txtStringTable {
public:
    txtStringTable();
    ~txtStringTable();

    void Load(const char* name, int language = 0);
    // Language-specific load: "<name>_<lang>" when present, else <name>; flags select subsets (recorded).
    void Load(const char* name, int language, int flags);
    // Merge one more section into what is already loaded instead of replacing it: a
    // screen that needs two sections (the garage needs its own strings alongside the
    // frontend's) loads the first and appends the rest.  Does nothing when the disc
    // has no table for that section, so assets without one are unaffected.
    void LoadAppend(const char* name, int language, int flags);
    void SetDefaultFont(const char *fontName);
    // Missing strings: return the key silently instead of warning.
    static void SetMissingModeIgnore(bool ignore = true) { sm_MissingModeIgnore = ignore; }
    static bool sm_MissingModeIgnore;
    void Kill();

    // A miss returns a placeholder whose text is the key wrapped in percent
    // signs (%CM_vp_lancer_04%) rather than an empty string, so a key the
    // loaded table does not carry is visible on screen instead of drawing a
    // blank row.  Never returns null.
    txtStringData* Get(const char* id);
    txtStringData* Get(int id);
    bool Exists(const char* id);
    // True once Load() has populated the table.
    bool IsValid() const { return m_Loaded; }

private:
    char m_DefaultFont[32] = "";   // SetDefaultFont: font for entries without one

    bool m_Loaded = false;
    bool m_Appending = false;      // LoadAppend: keep what is already in the table
    HashTable m_HashTable;
    std::vector<txtStringData*> m_AllocatedData;
    txtStringData m_DummyData;

    // Placeholders for missing keys.  A ring, because callers routinely
    // resolve two ids in one expression before using either; the buffers are
    // members so Kill() (which only frees m_AllocatedData) never touches them.
    enum { kMissingSlots = 8, kMissingChars = 96 };
    txtStringData m_MissingData[kMissingSlots];
    wchar_t       m_MissingText[kMissingSlots][kMissingChars];
    int           m_MissingSlot = 0;
    txtStringData* MakeMissing(const char* id);
};

extern txtStringTable STRINGTABLE;

#endif // TEXT_STRINGTABLE_H
