#ifndef PARSE_FILEIO_H
#define PARSE_FILEIO_H

////////////////////////////////////////
// parse/fileio.h
//
// parFileIO - base class for "tune" objects that load/save themselves through
// a datParser.  A subclass names its data folder (GetDirName), its class
// (GETCLASS(x) - also the file extension), binds its members in FileIO(), and
// calls SetName()+Load() to read <dir>/<name>.<class>.
//
// Clean-room port for the AGE 2.72 / MC3 surface.
////////////////////////////////////////

#include "core/output.h"
#include "core/types.h"
#include "data/assetcfg.h"
#include "data/base.h"
#include "data/parser.h"

class datResource;
class bkBank;

// Declares the type-name accessor used as the tune file extension.
#define GETCLASS(x) virtual const char *GetTypeName() { return #x; }

class parFileIO : public Base
{
public:
	enum {
		PARSER_HASH   = 1 << 0,   // keys are hashed (accepted; the parser is name-driven either way)
		PARSER_LOADED = 1 << 1,   // set once Load() has succeeded
		PARSER_NOWARN = 1 << 2    // suppress the missing-file warning
	};

	parFileIO();
	parFileIO(datResource &rsc);
	virtual ~parFileIO();

	// Non-const on purpose: the game overrides it non-const (ptx, mcFog, mcSpotlight, ...),
	// and a const virtual here would never be overridden.
	virtual const char *GetTypeName()				{ return "parFileIO"; }
	const char *GetClassName() const				{ return const_cast<parFileIO *>(this)->GetTypeName(); }
	virtual const char *GetDirName()				{ return ""; }

	// Called before the first Load so subclasses can size dynamic storage.
	// Empty at this level: the base object has nothing to size.  mcCarType is
	// the one override today.
	virtual void Allocate()							{ }

	// Called by Load after every parse (loaded or not), as the alpha's Load
	// does through vtable slot 4; derive state from the freshly read values.
	// Empty at this level (the alpha's body at 0x513718 is "jr ra").
	virtual void AfterLoad()						{ }

	// Bind members to the parser.  Subclasses call parFileIO::FileIO(p) first.
	virtual void FileIO(datParser &p);

#if __BANK
	// Bank widgets: Load/Save buttons plus the file name.
	virtual void AddWidgets(bkBank &b);
#endif

	void SetName(const char *name);
	const char *GetName() const						{ return m_Name; }

	bool Load();
	bool Load(const char *name);
	bool Load(const char *name, bool noWarn) { if (noWarn) SetFlag(PARSER_NOWARN); return Load(name); }
	bool Save();
	bool Reload()									{ return Load(); }

	void SetFlag(int flag)							{ m_Flags |= flag; }
	void ClearFlag(int flag)						{ m_Flags &= ~flag; }
	int  GetFlag() const							{ return m_Flags; }
	bool IsLoaded() const							{ return (m_Flags & PARSER_LOADED) != 0; }

protected:
	// Bank callbacks.
	void LoadCB();
	void SaveCB();

	char	m_Name[64];
	int		m_Flags;
};


// Marks FileIO method bodies (game sources wrap them in #ifdef __fileio).
#ifndef __fileio
#define __fileio 1
#endif

#endif // PARSE_FILEIO_H
