////////////////////////////////////////
// assetcfg.h
////////////////////////////////////////

#ifndef DATA_ASSETCFG_H
#define DATA_ASSETCFG_H

#include "core/stream.h"
#include "data/token.h"
#include <string.h>

inline bool sHasExtension(const char *base, const char *ext) {
  if (!base || !ext) return false;
  size_t blen = strlen(base), elen = strlen(ext);
  return blen > elen && base[blen - elen - 1] == '.' &&
         _stricmp(base + blen - elen, ext) == 0;
}

inline bool NeedsPath(const char* base) {
    if (!base) return false;
    if (base[0] == '/' || base[0] == '\\') return false;
    if (base[0] != '\0' && base[1] == ':') return false;
    return true;
}

inline void AddExtension(char* dest, const char* base, const char* ext) {
    char cleanBase[256] = "";
    if (base) {
        strncpy(cleanBase, base, sizeof(cleanBase) - 1);
        cleanBase[sizeof(cleanBase) - 1] = '\0';
        int len = (int)strlen(cleanBase);
        while (len > 0 && (cleanBase[len - 1] == ' ' || cleanBase[len - 1] == '\t' || cleanBase[len - 1] == '\r' || cleanBase[len - 1] == '\n')) {
            cleanBase[--len] = '\0';
        }
        if (ext && ext[0] != '\0') {
            int elen = (int)strlen(ext);
            if (len > elen + 1 && cleanBase[len - elen - 1] == '.' && _stricmp(cleanBase + len - elen, ext) == 0) {
                cleanBase[len - elen - 1] = '\0';
            }
        }
    }

    strcat(dest, cleanBase);
    if (ext && ext[0] != '\0') {
        strcat(dest, ".");
        strcat(dest, ext);
    }
}

class datAssetManager {
public:
    static datAssetManager *sm_Instance;
    static void (*sm_OpenHook)();

    char m_RootPath[256];
    char m_CurrentFolder[256];

    // PC port: keep folder history so root-relative pushes ($/) can restore cleanly
    char m_FolderHistory[32][256];
    int m_FolderOffsets[32];
    int m_FolderStackDepth;

    char m_Extensions[8][16];
    int m_ExtensionDepth;
    bool m_IgnorePrefix;

    datAssetManager() {
        m_RootPath[0] = '\0';
        m_CurrentFolder[0] = '\0';
        m_FolderStackDepth = 0;
        m_ExtensionDepth = 0;
        m_IgnorePrefix = false;
        sm_Instance = this;
    }

    virtual ~datAssetManager() {}
    virtual Stream *Open(const char *base, const char *ext);
    // probeOnly suppresses the not-found diagnostic; behaviour is otherwise identical.
    virtual Stream *Open(const char *base, const char *ext, bool probeOnly) { return Open(base, ext); }
    virtual Stream *Open(const char *folder, const char *base, const char *ext);
    // AGE 2.72 forms: (base, ext, ignorePrefix, addExt) and the folder variant.
    // ignorePrefix bypasses the "$/" resource prefix handling; addExt is the
    // legacy "append the extension" flag (always on here).
    virtual Stream *Open(const char *base, const char *ext, bool ignorePrefix, bool addExt) { return Open(base, ext, ignorePrefix); }
    virtual Stream *Open(const char *folder, const char *base, const char *ext, bool ignorePrefix, bool addExt);
    void Close(Stream *s);   // symmetric with Open (closes and disowns)

    // Enumerate the entries of an asset folder (files and subdirectories).
    // forceRaw bypasses the asset root and reads the path as given.
    typedef void (*EnumCallback)(const char *filename, bool isDirectory, void *userData);
    virtual int EnumFiles(const char *folder, EnumCallback cb, void *userData, bool forceRaw = false);

    // Extension stack: PushExtension("x") makes Open try base.x.ext first.
    virtual void PushExtension(const char *ext);
    virtual void PopExtension();
    // Strip folder and extension from a path.
    void BaseName(const char *path, char *dest, int maxLen) const;
    void SetIgnorePrefixFlag(bool ignore) { m_IgnorePrefix = ignore; }
    bool GetIgnorePrefixFlag() const { return m_IgnorePrefix; }

    // Enumerate the subdirectories of an asset folder, sorted alphabetically
    // (case-insensitive).  names is a caller buffer of maxCount slots, each
    // `stride` bytes.  Returns the number filled (-runall layout sweep).
    int ListFolders(const char *folder, char *names, int stride, int maxCount);
    virtual Stream *Create(const char *base, const char *ext);
    virtual Stream *Create(const char *folder, const char *base, const char *ext);
    virtual Stream *Create(const char *folder, const char *base, const char *ext, bool ignorePrefix) { return Create(folder, base, ext); }
    // Full path of folder/base.ext without touching the folder stack.
    virtual void FullPath(char *dest, int maxLen, const char *folder, const char *base, const char *ext);

    virtual void PushFolder(const char *folder) {
        if (folder) {
            if (m_FolderStackDepth < 32) {
                strncpy(m_FolderHistory[m_FolderStackDepth], m_CurrentFolder, sizeof(m_FolderHistory[0]) - 1);
                m_FolderHistory[m_FolderStackDepth][sizeof(m_FolderHistory[0]) - 1] = '\0';
                m_FolderOffsets[m_FolderStackDepth] = (int)strlen(m_CurrentFolder);
                m_FolderStackDepth++;
            }
            // PC port: '$/' or '$\\' indicates path relative to asset root
            if (folder[0] == '$' && (folder[1] == '/' || folder[1] == '\\')) {
                strncpy(m_CurrentFolder, folder + 2, sizeof(m_CurrentFolder) - 1);
                m_CurrentFolder[sizeof(m_CurrentFolder) - 1] = '\0';
            } else {
                if (m_CurrentFolder[0] != '\0') {
                    strcat(m_CurrentFolder, "/");
                }
                strcat(m_CurrentFolder, folder);
            }
        }
    }

    virtual void PopFolder() {
        if (m_FolderStackDepth > 0) {
            m_FolderStackDepth--;
            strncpy(m_CurrentFolder, m_FolderHistory[m_FolderStackDepth], sizeof(m_CurrentFolder) - 1);
            m_CurrentFolder[sizeof(m_CurrentFolder) - 1] = '\0';
        } else {
            char *slash = strrchr(m_CurrentFolder, '/');
            if (slash) {
                *slash = '\0';
            } else {
                m_CurrentFolder[0] = '\0';
            }
        }
    }

    // Asset root + existence query (used by asset-consuming apps, e.g. testanim2).
    const char* GetPath() const { return m_RootPath; }
    virtual void SetPath(const char *path) {
        if (path) {
            strncpy(m_RootPath, path, sizeof(m_RootPath) - 2);
            m_RootPath[sizeof(m_RootPath) - 2] = '\0';
            size_t len = strlen(m_RootPath);
            if (len > 0 && m_RootPath[len - 1] != '/' && m_RootPath[len - 1] != '\\') {
                m_RootPath[len] = '/';
                m_RootPath[len + 1] = '\0';
            }
        }
    }
    virtual bool Exists(const char *base, const char *ext);
    virtual bool Exists(const char *subfolder, const char *base, const char *ext);
    virtual void FullPath(char *dest, int maxLen, const char *base, const char *ext);

    // The active manager: an explicitly-installed override (a game's
    // rbAssetManager sets sm_Instance) or a lazily-created default.  Callers reach
    // it through the ASSET macro, so the override is resolved ONCE here at the call
    // site instead of via a per-method "if (sm_Instance != this)" redirect.
    static datAssetManager *GetActive();
};

class datAssetManagerHier : public datAssetManager {
public:
    virtual ~datAssetManagerHier() {}
};

typedef datAssetManagerHier datAssetManagerHierNew;

class datAssetManagerFlat : public datAssetManagerHier {
public:
    virtual ~datAssetManagerFlat() {}
};

inline datAssetManager *datAssetManager::GetActive() {
    if (!sm_Instance) sm_Instance = new datAssetManagerHier();
    return sm_Instance;
}

// The global asset manager resolves to whichever instance is currently active.
#define ASSET (*datAssetManager::GetActive())

#endif // DATA_ASSETCFG_H
