#ifndef PHCORE_POLY_H
#define PHCORE_POLY_H

////////////////////////////////////////
// phcore/poly.h
//
// phPolygon - one collision face of a polyhedral bound: up to four vertex
// indices into the owning bound's vertex array, plus the cached unit normal
// and centre.
////////////////////////////////////////

#include "vector/vector3.h"

class phSegment;
class phIntersection;

struct phPolygon {
	Vector3 Normal;
	Vector3 Center;
	int NumVertices;
	int Vertices[4];

	Vector3 GetUnitNormal() const { return Normal; }
	Vector3 GetNormal() const { return Normal; }
	int GetNumVerts() const { return NumVertices; }
	int GetIndex(int i) const { return (i >= 0 && i < 4) ? Vertices[i] : 0; }

	// Segment test that only accepts hits from the front side (segment
	// travelling against the normal).  `verts` is the bound's vertex array.
	// `edgeTolerance` widens the inside test (world units) so a probe that
	// grazes an edge still reports the face; on a hit `isect` receives the
	// point, normal and segment T.
	bool TestSegmentDirected(const Vector3 *verts, const phSegment &seg, phIntersection *isect, float edgeTolerance = 0.0f) const;
};

typedef phPolygon phPoly;

#endif // PHCORE_POLY_H
