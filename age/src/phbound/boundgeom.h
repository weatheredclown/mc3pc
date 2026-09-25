#ifndef PHBOUND_BOUNDGEOM_H
#define PHBOUND_BOUNDGEOM_H

#include "phbound/bound.h"

class phBoundGeometry : public phBound {
public:
	phBoundGeometry() {}
	virtual ~phBoundGeometry() {}

	virtual int GetType() const { return GEOMETRY; }
	virtual bool IsPolygonal() const { return true; }

	int GetNumVertices() const { return Vertices.GetCount(); }
	int GetNumMaterials() const { return Materials.GetCount(); }
	int GetNumPolygons() const { return PolyMaterials.GetCount(); }
	void Init(int numVerts, int numMats, int numPolys) {
		Vertices.Reserve(numVerts);
		Materials.Reserve(numMats);
		PolyMaterials.Reserve(numPolys);
	}
	virtual void Copy(const phBound *src) {
		phBound::Copy(src);
		if (src) {
			Vertices = src->Vertices;
			TriVerts = src->TriVerts;
		}
	}
};

#endif // PHBOUND_BOUNDGEOM_H
