#include "text/stringtable.h"
#include "text/fonttex.h"
#include "core/stream.h"
#include "data/assetcfg.h"
#include "core/output.h"
#include <stdio.h>
#include <string.h>


const wchar_t *txtStringData::GetString() const {
  return mString ? mString : L"";
}

// Resolved lazily so string tables can load before the gfx device is up.
gfxFont *txtStringData::GetFont() const {
  if (!m_FontResolved) {
    m_FontResolved = true;
    if (m_FontName[0])
      m_FontTex = txtGetFontTex(m_FontName);
  }
  return m_FontTex ? (gfxFont *)m_FontTex : &m_Font;
}

txtStringTable::txtStringTable() { m_DummyData.mString = L""; }

txtStringTable::~txtStringTable() { Kill(); }

#include <unordered_map>

static u32 HashStringV0(const char *str) {
  u32 hash = 0;
  while (*str) {
    signed char c = (signed char)*str++;
    hash = (hash << 4) + c;
    u32 mask = hash & 0xF0000000;
    if (mask) {
      hash ^= (mask >> 24) ^ mask;
    }
  }
  return hash;
}

static u32 HashStringV1(const char *str) {
  u32 hash = 0;
  while (*str) {
    signed char c = (signed char)*str++;
    hash = hash + c;
    hash = (hash << 10) + hash;
    hash ^= (hash >> 6);
  }
  hash = (hash << 3) + hash;
  hash ^= (hash >> 11);
  return (hash << 15) + hash;
}

void txtStringTable::Load(const char *name, int language) {
  m_Loaded = true;

  if (!m_Appending)
    Kill();

  SafeStream s = ASSET.Open(name, "strtbl");
  if (!s) {
    printf("[STRINGTABLE] ERROR: Failed to open string table '%s'\n",
           name ? name : "NULL");
    fflush(stdout);
    return;
  }

  u32 num_languages = 0;
  s->Read(&num_languages, 4);

  if (num_languages == 0 || num_languages > 128) {
    printf("[STRINGTABLE] ERROR: Invalid languages count %u\n", num_languages);
    fflush(stdout);
    return;
  }

  u32 *offsets = new u32[num_languages];
  s->Read(offsets, num_languages * 4);

  // Format revision, stored as version << 8 (0x200 = v2, the layout below).
  // See EdnessP's strtbl.py (Midnight Club RE community) for other revisions.
  u32 version = 0;
  s->Read(&version, 4);
  version >>= 8;

  u32 num_keys = 0;
  s->Read(&num_keys, 4);

  if (version != 2) {
    printf("[STRINGTABLE] ERROR: Unsupported strtbl format v%u\n", version);
    fflush(stdout);
    delete[] offsets;
    return;
  }

  if (num_keys > 65536) {
    printf(
        "[STRINGTABLE] ERROR: Sanity check failed, key count %u is too large\n",
        num_keys);
    fflush(stdout);
    delete[] offsets;
    return;
  }

  struct KeyRecord {
    char name[128];
  };
  KeyRecord *keys = new KeyRecord[num_keys];
  std::unordered_map<u32, const char *> key_map_v1;
  std::unordered_map<u32, const char *> key_map_v0;
  key_map_v1.reserve(num_keys);
  key_map_v0.reserve(num_keys);

  for (u32 i = 0; i < num_keys; i++) {
    u32 key_len = 0;
    s->Read(&key_len, 4);
    if (key_len >= 128) {
      printf("[STRINGTABLE] WARNING: Key length %u exceeds buffer size, "
             "truncating\n",
             key_len);
      fflush(stdout);
      key_len = 127;
    }
    s->Read(keys[i].name, key_len);
    keys[i].name[key_len] = '\0';

    char null_byte = 0;
    s->Read(&null_byte, 1);

    u32 hv1 = HashStringV1(keys[i].name);
    u32 hv0 = HashStringV0(keys[i].name);
    key_map_v1[hv1] = keys[i].name;
    key_map_v0[hv0] = keys[i].name;
  }

  u32 lang_offset_idx = 0;
  if (language >= 0 && (u32)language < num_languages) {
    lang_offset_idx = (u32)language;
  }
  s->Seek(offsets[lang_offset_idx]);

  u32 num_values = 0;
  s->Read(&num_values, 4);

  if (num_values > 65536) {
    printf("[STRINGTABLE] ERROR: Sanity check failed, value count %u is too "
           "large\n",
           num_values);
    fflush(stdout);
    delete[] keys;
    delete[] offsets;
    return;
  }

  // v2 value record:
  //   u32 key hash (HashStringV1 for MC3, HashStringV0 for earlier titles), u16 flags,
  //   u32 font-name length + chars (no null), u32 string length incl. null
  //   terminator + UTF-16LE chars, float scale x, float scale y,
  //   s8 offset x, s8 offset y.
  u32 matched_count = 0;
  for (u32 i = 0; i < num_values; i++) {
    u32 hash_val = 0;
    s->Read(&hash_val, 4);

    u16 flags = 0;
    s->Read(&flags, 2);

    u32 font_len = 0;
    s->Read(&font_len, 4);

    char font_name[128];
    if (font_len >= 128) {
      font_len = 127;
    }
    s->Read(font_name, font_len);
    font_name[font_len] = '\0';

    u32 str_len = 0;
    s->Read(&str_len, 4);

    wchar_t *wstr = new wchar_t[str_len + 1];
    s->Read(wstr, str_len * 2);
    wstr[str_len] = L'\0';

    float scale_x = 1.0f, scale_y = 1.0f;
    s->Read(&scale_x, 4);
    s->Read(&scale_y, 4);

    s8 offset_x = 0, offset_y = 0;
    s->Read(&offset_x, 1);
    s->Read(&offset_y, 1);

    const char *key_name = nullptr;
    auto it1 = key_map_v1.find(hash_val);
    if (it1 != key_map_v1.end()) {
      key_name = it1->second;
    } else {
      auto it0 = key_map_v0.find(hash_val);
      if (it0 != key_map_v0.end()) {
        key_name = it0->second;
      }
    }

    if (key_name) {
      matched_count++;
      txtStringData *stringData = new txtStringData();
      stringData->mString = wstr;
      stringData->m_ScaleX = scale_x;
      stringData->m_ScaleY = scale_y;
      stringData->m_OffsetX = offset_x;
      stringData->m_OffsetY = offset_y;
      stringData->m_Flags = flags;
      if (font_name[0]) {
        strncpy(stringData->m_FontName, font_name, sizeof(stringData->m_FontName) - 1);
      } else if (m_DefaultFont[0]) {
        strncpy(stringData->m_FontName, m_DefaultFont, sizeof(stringData->m_FontName) - 1);
      }
      stringData->m_FontName[sizeof(stringData->m_FontName) - 1] = '\0';

      m_AllocatedData.push_back(stringData);
      m_HashTable.Add(key_name, stringData);
    } else {
      delete[] wstr;
    }
  }

  delete[] keys;
  delete[] offsets;
}

void txtStringTable::Kill() {
  m_Loaded = false;
  for (txtStringData *data : m_AllocatedData) {
    if (data) {
      if (data->mString) {
        delete[] const_cast<wchar_t *>(data->mString);
        data->mString = nullptr;
      }
      delete data;
    }
  }
  m_AllocatedData.clear();
  m_HashTable.MakeEmpty();
}

// A miss used to come back as an empty string, so a key the loaded table does
// not carry drew a blank row and nothing else -- the port shipped for months
// with every "CM_<vehicle>" lookup silently missing behind exactly that.  Hand
// back the key wrapped in percent signs instead: %CM_vp_lancer_04% is obvious
// on screen and greppable in a capture or a log.
txtStringData *txtStringTable::MakeMissing(const char *id) {
  txtStringData *data = &m_MissingData[m_MissingSlot];
  wchar_t *text = m_MissingText[m_MissingSlot];
  m_MissingSlot = (m_MissingSlot + 1) % kMissingSlots;

  int n = 0;
  text[n++] = L'%';
  for (const char *s = id; *s && n < kMissingChars - 2; ++s)
    text[n++] = (wchar_t)(unsigned char)*s;
  text[n++] = L'%';
  text[n] = L'\0';

  // Leave m_FontName empty: GetFont() then falls back the way the dummy always
  // has, and every caller of a missing id wants the text, not the styling.
  data->mString = text;

  // Once per id: a missing HUD or menu key is otherwise re-resolved every
  // frame and drowns the log.
  if (!sm_MissingModeIgnore) {
    static std::unordered_map<u32, bool> warned;
    if (warned.insert(std::make_pair(HashStringV1(id), true)).second)
      Warningf("txtStringTable: no string for id '%s'", id);
  }
  return data;
}

txtStringData *txtStringTable::Get(const char *id) {
  // An absent or empty id is a caller deliberately asking for nothing, not a
  // data mismatch, so it still gets the silent empty string.
  if (!id || !id[0])
    return &m_DummyData;
  txtStringData *data = (txtStringData *)m_HashTable.Find(id);
  return data ? data : MakeMissing(id);
}

// Unchanged: the int overload never looked anything up, and Get(0) is how
// txtStringTex borrows entry 0's font and scale for a literal string.  A
// placeholder here would be noise from an unimplemented API rather than a
// real key/data mismatch - and SetString(int) would paint it on screen.
txtStringData *txtStringTable::Get(int id) { return &m_DummyData; }

bool txtStringTable::Exists(const char *id) {
  if (!id)
    return false;
  return m_HashTable.Find(id) != nullptr;
}

#include "text/language.h"
#include <stdio.h>
#include <string.h>

void txtStringTable::Load(const char *name, int language, int flags)
{
    // MC3 ships one table per section mask (the game passes 0x01 frontend, 0x02
    // in-game, 0x04 race editor): "mcstrings" + 0x02 -> fonts/mcstrings02.strtbl,
    // every language inside it.  The _pal variants belong to the PAL disc.
    char sectioned[128];
    if (flags) {
        snprintf(sectioned, sizeof(sectioned), "%s%02x", name, flags);
        if (ASSET.Exists(sectioned, "strtbl")) { Load(sectioned, language); return; }
    }
    char localized[128];
    const char *abbrev = txtLanguage::Get(language);
    if (abbrev && abbrev[0]) {
        snprintf(localized, sizeof(localized), "%s_%s", name, abbrev);
        if (ASSET.Exists(localized, "strtbl")) { Load(localized, language); return; }
    }
    Load(name, language);
}

// Merge a second section into the table.  Only the sectioned table is considered:
// falling back to the whole of "mcstrings" here would re-read everything already
// loaded, and a disc without that section simply has nothing to add.
void txtStringTable::LoadAppend(const char *name, int language, int flags)
{
    if (!flags)
        return;

    char sectioned[128];
    snprintf(sectioned, sizeof(sectioned), "%s%02x", name, flags);
    if (!ASSET.Exists(sectioned, "strtbl"))
        return;

    m_Appending = true;
    Load(sectioned, language);
    m_Appending = false;
}

void txtStringTable::SetDefaultFont(const char *fontName)
{
    if (!fontName) { m_DefaultFont[0] = 0; return; }
    strncpy(m_DefaultFont, fontName, sizeof(m_DefaultFont) - 1);
    m_DefaultFont[sizeof(m_DefaultFont) - 1] = 0;
}

bool txtStringTable::sm_MissingModeIgnore = false;
