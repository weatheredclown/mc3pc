////////////////////////////////////////
// data/param.cpp
//
// datParam - the PARAM("name", "description") command-line parameter system.
//
// Every PARAM() macro declares a file-scope datParam whose constructor links it
// into a registry, so the game can list what it accepts and warn about a flag
// nobody declared.  A parameter's value always comes from the args singleton;
// datParam is the declaration side, args is the storage side.
////////////////////////////////////////

#include "data/param.h"

#include "core/output.h"
#include "data/args.h"

#include <string.h>

// Zero-initialised before any dynamic initialiser runs, so a PARAM() global in
// any translation unit can link itself here from its constructor without
// depending on static initialisation order between translation units.
static datParam *s_FirstParam;
static int s_NumParams;

datParam::datParam(const char *name, const char *desc)
	: Name(name), Desc(desc), Next(s_FirstParam)
{
	s_FirstParam = this;
	s_NumParams++;
}

const datParam *datParam::GetFirst()
{
	return s_FirstParam;
}

int datParam::GetCount()
{
	return s_NumParams;
}

const datParam *datParam::Find(const char *name)
{
	if (!name) return NULL;
	for (const datParam *p = s_FirstParam; p; p = p->Next)
		if (p->Name && !_stricmp(p->Name, name))
			return p;
	return NULL;
}

bool datParam::Get() const
{
	return (Name && args::sm_Instance) ? args::sm_Instance->Get(Name) : false;
}

void datParam::PrintUsage()
{
	Displayf("Command line parameters (%d declared):", s_NumParams);
	for (const datParam *p = s_FirstParam; p; p = p->Next)
		Displayf("  -%-28s %s", p->Name ? p->Name : "?", p->Desc ? p->Desc : "");
}

void datParam::Init(int argc, char **argv)
{
	// main() calls this before ARGS.Init(), and every PARAM_x.Get() reads the
	// args singleton, so the command line is installed here.  args::Init just
	// copies the vector, and main's own call repeats it harmlessly.
	if (!args::sm_Instance)
		return;								// no args object yet: nothing can be queried
	args::sm_Instance->Init(argc, argv);

	if (args::sm_Instance->Get("help") || args::sm_Instance->Get("?"))
	{
		PrintUsage();
		return;
	}

	// -paramcheck lists flags the build never declared.  A mistyped switch
	// otherwise looks like it worked, because args::Get simply never matches it.
	// Off by default and never fatal: plenty of live flags are read straight
	// through ARGS without a PARAM() declaration, so an undeclared name here is
	// a hint, not an error.
	if (args::sm_Instance->Get("paramcheck"))
	{
		for (int i = 1; i < args::sm_Instance->Argc; i++)
		{
			const char *a = args::sm_Instance->Argv[i];
			if (!a || a[0] != '-' || !a[1]) continue;
			if (a[1] >= '0' && a[1] <= '9') continue;	// a negative number, not a flag
			if (!Find(a + 1))
				Displayf("datParam: '-%s' is not a declared parameter", a + 1);
		}
	}
}
