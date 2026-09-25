#include "parse/fileio.h"

#include "bank/bank.h"
#include "core/output.h"
#include "data/assetcfg.h"
#include "data/callback.h"
#include "data/parser.h"
#include "data/resource.h"

#include <string.h>

parFileIO::parFileIO()
{
	m_Name[0] = '\0';
	m_Flags = 0;
}

parFileIO::parFileIO(datResource &rsc)
{
	// Page-in constructor.  PS2 layout: vtable, const char *name, int flags (12 bytes);
	// the name string sits elsewhere in the image, so copy it.
	m_Name[0] = 0;
	m_Flags = 0;
	if (rsc.IsValid()) {
		rsc.GetVTable();
		rsc.GetString(m_Name, sizeof(m_Name));
		m_Flags = rsc.GetInt();
	}
}

parFileIO::~parFileIO()
{
}

void parFileIO::SetName(const char *name)
{
	if (!name) {
		m_Name[0] = '\0';
		return;
	}
	strncpy(m_Name, name, sizeof(m_Name) - 1);
	m_Name[sizeof(m_Name) - 1] = '\0';
}

void parFileIO::FileIO(datParser &)
{
	// Nothing bound at this level; subclasses add their members.
}

bool parFileIO::Load(const char *name)
{
	SetName(name);
	return Load();
}

bool parFileIO::Load()
{
	if (m_Name[0] == '\0') {
		Warningf("parFileIO::Load - %s has no file name", GetTypeName());
		return false;
	}

	Allocate();

	const char *dir = GetDirName();
	if (dir && dir[0]) ASSET.PushFolder(dir);

	datParser p(GetTypeName());
	p.SetNoWarnings((m_Flags & PARSER_NOWARN) != 0);
	FileIO(p);
	// The DUB source names each file "<name>.<class>" in the class's folder.  The
	// Remix disc ships them as "tune/<name>.parfileio" regardless of class (the
	// city conditions, paint jobs, colour libraries...), so a miss on the class
	// spelling falls back to that before warning.
	bool primaryExists = ASSET.Exists(m_Name, GetTypeName());
	bool ok = false;
	if (primaryExists) {
		ok = p.Load(m_Name, GetTypeName());
	} else {
		bool save = p.GetNoWarnings();
		p.SetNoWarnings(true);
		ASSET.PushFolder("tune");
		ok = p.Load(m_Name, "parfileio");
		ASSET.PopFolder();
		p.SetNoWarnings(save);
		if (!ok) ok = p.Load(m_Name, GetTypeName());      // warns as before
	}

	if (dir && dir[0]) ASSET.PopFolder();

	if (ok) m_Flags |= PARSER_LOADED;

	// The alpha's parFileIO::Load (0x5135f0) calls the AfterLoad virtual after
	// every parse, loaded or not; tune objects derive state there (HUD
	// elements anchor their screen position, sound/audio data rebuild curves).
	AfterLoad();
	return ok;
}

bool parFileIO::Save()
{
	if (m_Name[0] == '\0') return false;

	const char *dir = GetDirName();
	char path[512];
	path[0] = '\0';
	if (dir && dir[0]) ASSET.PushFolder(dir);
	ASSET.FullPath(path, sizeof(path), m_Name, GetTypeName());
	if (dir && dir[0]) ASSET.PopFolder();

	datParser p(GetTypeName());
	FileIO(p);
	return p.Save(path, 0);
}

void parFileIO::LoadCB()
{
	Load();
}

void parFileIO::SaveCB()
{
	Save();
}

#if __BANK
void parFileIO::AddWidgets(bkBank &b)
{
	b.AddText("File", m_Name, sizeof(m_Name));
	b.AddButton("Load", datCallback(MFA(parFileIO::LoadCB), this));
	b.AddButton("Save", datCallback(MFA(parFileIO::SaveCB), this));
}
#endif // __BANK
