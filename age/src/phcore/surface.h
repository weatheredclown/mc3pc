#ifndef PHCORE_SURFACE_H
#define PHCORE_SURFACE_H

#include "core/types.h"
#include "phcore/material.h"
#include "phcore/materialmgr.h"

class phSurface : public phMaterial {
public:
	enum { SURFACE = 100 };

	float Drag;
	float Depth;
	int   SndFx;
	short PtxIndex[4];

	phSurface();
	virtual ~phSurface();

	virtual int GetClassType() const { return SURFACE; }

	float GetDrag() const { return Drag; }
	void  SetDrag(float d) { Drag = d; }
	float GetDepth() const { return Depth; }
	void  SetDepth(float d) { Depth = d; }
	// Numeric effect id when the material's Effect string is a number
	// (feedback code switches on 0 cobblestone / 1 offroad / 2 bumpy); -1 otherwise.
	int   GetEffect() const;
	int   GetSndFx() const { return SndFx; }
	void  SetSndFx(int s) { SndFx = s; }
	int   GetPtxIndex(int i) const { return (i >= 0 && i < 4) ? PtxIndex[i] : -1; }
	void  SetPtxIndex(int i, short val) { if (i >= 0 && i < 4) PtxIndex[i] = val; }

	void  Copy(const phSurface &other);
	void  Copy(const phMaterial &other);

	phMaterial *GetMaterial() { return this; }
	const phMaterial *GetMaterial() const { return this; }
};

class phSurfaceMgr : public phMaterialMgr {
public:
	static phSurfaceMgr *Instance;

	phSurfaceMgr();
	virtual ~phSurfaceMgr();

	static phSurfaceMgr &GetInstance() { return *Instance; }
	static void CreateInstance();
	static void DeleteInstance();

	static phSurface *GetDefaultSurface();
	phMaterial *GetDefaultPhysMaterial();

	void LoadAll(const char *dir, const char *list, const char *ext);
#if __BANK
	void AddWidgets(class bkBank &bank);
#endif
};

#endif // PHCORE_SURFACE_H
