////////////////////////////////////////
// args.h
////////////////////////////////////////

#ifndef DATA_ARGS_H
#define DATA_ARGS_H

#include "core/types.h"

class args {
public:
    static args *sm_Instance;

    args();
    void Init(int argc = 0, char **argv = nullptr);

    bool Get(const char *name) const;
    bool Get(const char *name, int index, const char **value) const;
    bool Get(const char *name, int defaultVal, int &outVal) const;
    // These two were stubs that always returned false, so "-drawdist 600" and
    // every other float/bool argument silently did nothing.  They parse the
    // value when the argument is present and, unlike the int overload, leave
    // the caller's variable untouched when it is absent (they are override
    // hooks over values the tune files have already set).
    bool Get(const char *name, float unusedDefault, float &outVal) const;
    bool Get(const char *name, bool unusedDefault, bool &outVal) const;
    int GetNum(const char *name) const;

    // Ensure "-name [value]" is present, replacing an existing value (the
    // editor forces -usedi / -nocutscenes this way).
    void AddReplace(const char *name, const char *value);

    // Heap-blowout support: flatten the argument list to a string that
    // survives the heap being torn down, and rebuild from it.
    void SaveToArchive(char *dest, int maxLen = 1024) const;
    void RestoreFromArchive(const char *src);
    void Kill();

    int Argc;
    char *Argv[128];

private:
    char *m_Restored;   // storage owned by RestoreFromArchive
};

#define ARGS (*args::sm_Instance)

#define ARGS_SET_DEBUG_LEVEL(x) x = ARGS.Get(#x, 0, x)

#endif // DATA_ARGS_H
