#ifndef PHBOUND_BOUNDPOLYHEDRON_H
#define PHBOUND_BOUNDPOLYHEDRON_H

#include "phbound/bound.h"
#include "phcore/poly.h"

class phBoundPolyhedron : public phBound {
public:
    phBoundPolyhedron() {}
    virtual ~phBoundPolyhedron() {}

    virtual int GetType() const { return POLYHEDRON; }
    virtual bool IsPolygonal() const { return true; }
    const phPolygon& GetPolygon(int index) const {
        static phPolygon dummy;
        return dummy;
    }
    // Raw vertex array (polygon segment tests index into it).
    const Vector3* GetVertexPointer() const { return Vertices.GetCount() ? &Vertices[0] : nullptr; }
    const Vector3& GetVertex(int index) const {
        if (index >= 0 && index < Vertices.GetCount()) {
            return Vertices[index];
        }
        static Vector3 zero(0.0f, 0.0f, 0.0f);
        return zero;
    }
};

#endif // PHBOUND_BOUNDPOLYHEDRON_H
