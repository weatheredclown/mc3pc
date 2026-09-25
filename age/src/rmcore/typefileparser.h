#ifndef RMCORE_TYPEFILEPARSER_H
#define RMCORE_TYPEFILEPARSER_H

#include "data/token.h"
#include "data/callback.h"
#include "data/assetcfg.h"
#include "core/output.h"
#include <string.h>

struct rmcTypeFileCbData {
    datTokenizer *m_T;
    const char *m_Name;
    const char *m_EntityName;
    void *m_UserData;

    rmcTypeFileCbData(datTokenizer *tok = nullptr) : m_T(tok), m_Name(nullptr), m_EntityName(nullptr), m_UserData(nullptr) {}
};

class rmcTypeFileParser {
public:
    static const int MAX_LOADERS = 32;

    struct LoaderEntry {
        char key[64];
        char ext[64];
        datCallback cb;
    };

    LoaderEntry m_Loaders[MAX_LOADERS];
    int m_NumLoaders;
    bool m_WarningSpew;

    inline rmcTypeFileParser() : m_NumLoaders(0), m_WarningSpew(true) {}
    inline virtual ~rmcTypeFileParser() {}

    inline void Reset() { m_NumLoaders = 0; }
    inline void SetWarningSpew(bool spew) { m_WarningSpew = spew; }
    inline void Register(const char *key, const datCallback &cb) { RegisterLoader(key, "all", cb); }

    inline void RegisterLoader(const char *key, const char *ext, const datCallback &cb) {
        if (m_NumLoaders >= MAX_LOADERS) {
            Warningf("rmcTypeFileParser: max loaders exceeded");
            return;
        }
        strncpy(m_Loaders[m_NumLoaders].key, key ? key : "", sizeof(m_Loaders[m_NumLoaders].key) - 1);
        m_Loaders[m_NumLoaders].key[sizeof(m_Loaders[m_NumLoaders].key) - 1] = '\0';

        strncpy(m_Loaders[m_NumLoaders].ext, ext ? ext : "", sizeof(m_Loaders[m_NumLoaders].ext) - 1);
        m_Loaders[m_NumLoaders].ext[sizeof(m_Loaders[m_NumLoaders].ext) - 1] = '\0';

        m_Loaders[m_NumLoaders].cb = cb;
        m_NumLoaders++;
    }

    inline bool Parse(datTokenizer &tok) { return ProcessTypeFile(tok); }
    inline bool Parse(const char *name) { return ProcessTypeFile(name); }

    inline bool ProcessTypeFile(const char *name) {
        char base[256];
        strncpy(base, name, sizeof(base) - 1);
        base[sizeof(base) - 1] = '\0';
        size_t n = strlen(base);
        if (n > 5 && _stricmp(base + n - 5, ".type") == 0)
            base[n - 5] = '\0';

        Stream *s = ASSET.Open(base, "type");
        if (!s) {
            if (m_WarningSpew)
                Warningf("rmcTypeFileParser: cannot open '%s.type'", base);
            return false;
        }
        datTokenizer tok;
        tok.Init(base, s);
        bool ok = ProcessTypeFile(tok);
        s->Close();
        return ok;
    }

    inline bool ProcessTypeFile(datTokenizer &T) {
        char tok[256];
        while (T.GetToken(tok, sizeof(tok))) {
            if (_stricmp(tok, "version:") == 0) {
                T.GetInt();
                continue;
            }

            LoaderEntry *matched = nullptr;
            for (int i = 0; i < m_NumLoaders; i++) {
                if (_stricmp(m_Loaders[i].key, tok) == 0) {
                    matched = &m_Loaders[i];
                    break;
                }
            }

            char nextTok[256];
            if (!T.GetToken(nextTok, sizeof(nextTok)))
                break;

            if (strcmp(nextTok, "{") == 0) {
                int depth = 1;
                while (depth > 0) {
                    char entry[256];
                    if (!T.GetToken(entry, sizeof(entry)))
                        break;
                    if (strcmp(entry, "{") == 0) {
                        depth++;
                        continue;
                    }
                    if (strcmp(entry, "}") == 0) {
                        depth--;
                        continue;
                    }
                    if (matched && depth == 1) {
                        rmcTypeFileCbData cbData(&T);
                        cbData.m_Name = matched->key;
                        cbData.m_EntityName = entry;
                        matched->cb.Call(&cbData);
                    }
                }
            } else {
                // Unbraced single-token field
                if (matched) {
                    rmcTypeFileCbData cbData(&T);
                    cbData.m_Name = matched->key;
                    cbData.m_EntityName = nextTok;
                    matched->cb.Call(&cbData);
                }
            }
        }
        return true;
    }
};

#endif // RMCORE_TYPEFILEPARSER_H
