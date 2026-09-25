////////////////////////////////////////
// args.cpp
////////////////////////////////////////

#include "data/args.h"
#include <string.h>

args *args::sm_Instance = NULL;
static args s_ArgsInstance;

args::args() {
    sm_Instance = this;
    Argc = 0;
    memset(Argv, 0, sizeof(Argv));
    m_Restored = NULL;
}

void args::SaveToArchive(char *dest, int maxLen) const {
    if (!dest || maxLen <= 0) return;
    dest[0] = '\0';
    int len = 0;
    for (int i = 0; i < Argc; i++) {
        const char *a = Argv[i] ? Argv[i] : "";
        int alen = (int)strlen(a);
        if (len + alen + 2 >= maxLen) break;
        if (len) dest[len++] = ' ';
        memcpy(dest + len, a, alen);
        len += alen;
        dest[len] = '\0';
    }
}

void args::RestoreFromArchive(const char *src) {
    Kill();
    if (!src || !src[0]) return;
    m_Restored = new char[strlen(src) + 1];
    strcpy(m_Restored, src);
    char *p = m_Restored;
    while (*p && Argc < (int)(sizeof(Argv) / sizeof(Argv[0]))) {
        while (*p == ' ') p++;
        if (!*p) break;
        Argv[Argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = '\0';
    }
}

void args::Kill() {
    Argc = 0;
    memset(Argv, 0, sizeof(Argv));
    delete[] m_Restored;
    m_Restored = NULL;
}

void args::Init(int argc, char **argv) {
    if (argc == 0 && argv == nullptr) {
        if (Argc > 0) return;
    }
    Argc = argc < 128 ? argc : 128;
    for (int i = 0; i < Argc; i++) {
        Argv[i] = argv[i];
    }
#if defined(__WIN32PC)
    if (!Get("resources") && !Get("noresources")) {
        AddReplace("resources", nullptr);
    }
#endif
}

void args::AddReplace(const char *name, const char *value) {
    char flag[128];
    flag[0] = '-';
    strncpy(flag + 1, name ? name : "", sizeof(flag) - 2);
    flag[sizeof(flag) - 1] = 0;
    for (int i = 1; i < Argc; i++) {
        if (Argv[i] && _stricmp(Argv[i], flag) == 0) {
            if (value && value[0] && i + 1 < Argc && Argv[i + 1] && Argv[i + 1][0] != '-') {
                char *nv = new char[strlen(value) + 1];
                strcpy(nv, value);
                Argv[i + 1] = nv;
            }
            return;
        }
    }
    if (Argc < (int)(sizeof(Argv) / sizeof(Argv[0])) - 2) {
        char *nf = new char[strlen(flag) + 1];
        strcpy(nf, flag);
        Argv[Argc++] = nf;
        if (value && value[0]) {
            char *nv = new char[strlen(value) + 1];
            strcpy(nv, value);
            Argv[Argc++] = nv;
        }
    }
}

bool args::Get(const char *name) const {
    if (!name) return false;
    size_t nlen = strlen(name);
    for (int i = 1; i < Argc; i++) {
        if (!Argv[i]) continue;
        const char *p = (Argv[i][0] == '-' || Argv[i][0] == '/') ? &Argv[i][1] : Argv[i];
        if (_stricmp(p, name) == 0 || (_strnicmp(p, name, nlen) == 0 && p[nlen] == '=')) {
            return true;
        }
    }
    return false;
}

bool args::Get(const char *name, int index, const char **value) const {
    if (!name) return false;
    size_t nlen = strlen(name);
    int skip_count = 0;
    for (int i = 1; i < Argc; i++) {
        if (!Argv[i]) continue;
        const char *p = (Argv[i][0] == '-' || Argv[i][0] == '/') ? &Argv[i][1] : Argv[i];
        bool is_eq = (_strnicmp(p, name, nlen) == 0 && p[nlen] == '=');
        bool is_exact = (_stricmp(p, name) == 0);

        if (is_eq) {
            if (skip_count == index) {
                if (value) {
                    *value = &p[nlen + 1];
                    return true;
                }
                return false;
            }
            skip_count++;
        } else if (is_exact) {
            if (skip_count == index) {
                if (i + 1 < Argc && value) {
                    *value = Argv[i + 1];
                    return true;
                }
                return false;
            }
            skip_count++;
        }
    }
    return false;
}

int args::GetNum(const char *name) const {
    if (!name) return 0;
    size_t nlen = strlen(name);
    int count = 0;
    for (int i = 1; i < Argc; i++) {
        if (!Argv[i]) continue;
        const char *p = (Argv[i][0] == '-' || Argv[i][0] == '/') ? &Argv[i][1] : Argv[i];
        if (_stricmp(p, name) == 0 || (_strnicmp(p, name, nlen) == 0 && p[nlen] == '=')) {
            count++;
        }
    }
    return count;
}

#include <stdlib.h>
// Like the float/bool forms below: an absent argument leaves outVal ALONE.  The
// alpha's datArgParser::Get(const char *, unsigned int, int &) (0x3eb7d0) only
// writes on a hit - its middle argument is a value index, not a default - and the
// game seeds the variable first: "int maxOpponents = kMaxOpponents;
// ARGS.Get("maxopponents",0,maxOpponents)".  Writing the 0 zeroed every race's
// opponents and armed -autorunpasses in normal play.  The value itself still comes
// from the first occurrence (port callers pass the current value, e.g. "width").
bool args::Get(const char *name, int /*indexOrCurrent*/, int &outVal) const {
    const char *strVal = nullptr;
    if (Get(name, 0, &strVal) && strVal) {
        outVal = atoi(strVal);
        return true;
    }
    return false;
}

// These two are OVERRIDE hooks: the game seeds the variable from its tune data
// and then offers a command line override ("ARGS.Get(\"aiTurnConst\", 0, m_TurnConst)").
// So an absent argument must leave the caller's value ALONE -- writing the
// nominal default here would wipe every AI constant the tune files just set.
// (They used to be header stubs returning false, which had that behaviour by
// accident; the value is now actually parsed when the argument IS present.)
bool args::Get(const char *name, float /*unusedDefault*/, float &outVal) const {
    const char *strVal = nullptr;
    if (Get(name, 0, &strVal) && strVal) {
        outVal = (float)atof(strVal);
        return true;
    }
    return false;
}

bool args::Get(const char *name, bool /*unusedDefault*/, bool &outVal) const {
    const char *strVal = nullptr;
    if (Get(name, 0, &strVal) && strVal) {
        // "-flag 0" / "-flag off" / "-flag false" turn it off, anything else on
        outVal = !(!_stricmp(strVal, "0") || !_stricmp(strVal, "off") || !_stricmp(strVal, "false") || !_stricmp(strVal, "no"));
        return true;
    }
    if (Get(name)) { outVal = true; return true; }      // a bare "-flag" is true
    return false;
}
