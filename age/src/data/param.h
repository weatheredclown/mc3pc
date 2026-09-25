#ifndef DATA_PARAM_H
#define DATA_PARAM_H

#include "core/output.h"
#include "data/args.h"

// datParam - one declared command-line parameter.
//
// PARAM("nofe", "skip the front end") defines a file-scope datParam whose
// constructor links it into a registry, so the build can list what it accepts
// (-help) and point out a flag nobody declared (-paramcheck).  The VALUE always
// comes from the args singleton: datParam is the declaration side, args is the
// storage side.  Implementation in data/param.cpp.
class datParam {
public:
    const char *Name;
    const char *Desc;
    datParam *Next;             // registry link, set by the constructor

    // Links itself into the registry; defined in data/param.cpp.
    datParam(const char *name = nullptr, const char *desc = nullptr);

    // Called from main() before ARGS.Init(): installs the command line into the
    // args singleton so every PARAM works from here on, then handles -help.
    // An empty body here would silently swallow the whole command line.
    static void Init(int argc = 0, char **argv = nullptr);
    static const datParam *GetFirst();
    static int GetCount();
    static const datParam *Find(const char *name);
    static void PrintUsage();

    bool Get() const;
};

#define PARAM(name, desc) datParam PARAM_##name(#name, desc)
#define DECLARE_PARAM(name) extern datParam PARAM_##name

#endif // DATA_PARAM_H
