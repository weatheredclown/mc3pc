#include "phcore/surface.h"
#include <string.h>

phSurface::phSurface() : Drag(0.0f), Depth(0.0f), SndFx(0)
{
	Type = SURFACE;
	for (int i = 0; i < 4; ++i) {
		PtxIndex[i] = -1;
	}
}

phSurface::~phSurface()
{
}

void phSurface::Copy(const phSurface &other)
{
	Type = other.Type;
	strncpy(Name, other.Name, sizeof(Name) - 1);
	Name[sizeof(Name) - 1] = 0;
	Elasticity = other.Elasticity;
	Friction = other.Friction;
	strncpy(Effect, other.Effect, sizeof(Effect) - 1);
	Effect[sizeof(Effect) - 1] = 0;
	strncpy(SoundName, other.SoundName, sizeof(SoundName) - 1);
	SoundName[sizeof(SoundName) - 1] = 0;
	Drag = other.Drag;
	Depth = other.Depth;
	SndFx = other.SndFx;
	for (int i = 0; i < 4; ++i) {
		PtxIndex[i] = other.PtxIndex[i];
	}
}

void phSurface::Copy(const phMaterial &other)
{
	Type = other.Type;
	strncpy(Name, other.Name, sizeof(Name) - 1);
	Name[sizeof(Name) - 1] = 0;
	Elasticity = other.Elasticity;
	Friction = other.Friction;
	strncpy(Effect, other.Effect, sizeof(Effect) - 1);
	Effect[sizeof(Effect) - 1] = 0;
	strncpy(SoundName, other.SoundName, sizeof(SoundName) - 1);
	SoundName[sizeof(SoundName) - 1] = 0;
}

phSurfaceMgr *phSurfaceMgr::Instance = nullptr;

static phSurface s_DefaultSurface;

phSurfaceMgr::phSurfaceMgr() : phMaterialMgr(0, 128)
{
	Instance = this;
}

phSurfaceMgr::~phSurfaceMgr()
{
	if (Instance == this) {
		Instance = nullptr;
	}
}

void phSurfaceMgr::CreateInstance()
{
	if (!Instance) {
		new phSurfaceMgr();
	}
}

void phSurfaceMgr::DeleteInstance()
{
	if (Instance) {
		delete Instance;
		Instance = nullptr;
	}
}

phSurface *phSurfaceMgr::GetDefaultSurface()
{
	return &s_DefaultSurface;
}

phMaterial *phSurfaceMgr::GetDefaultPhysMaterial()
{
	return &s_DefaultSurface;
}

void phSurfaceMgr::LoadAll(const char *dir, const char *list, const char *ext)
{
}

#if __BANK
void phSurfaceMgr::AddWidgets(class bkBank &bank)
{
}
#endif

#include <stdlib.h>
#include <ctype.h>

int phSurface::GetEffect() const
{
	const char *e = Effect;
	if (!e || !e[0]) return -1;
	for (const char *c = e; *c; c++)
		if (!isdigit((unsigned char)*c) && *c != '-') return -1;
	return atoi(e);
}
