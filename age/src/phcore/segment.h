#ifndef PHCORE_SEGMENT_H
#define PHCORE_SEGMENT_H

#include "vector/vector3.h"
#include "vector/geometry.h"

// Plain value type: filled in on the stack, then passed by const phSegment& into
// probe functions (PHLEVEL->TestProbe, fzxWaterSurface::TestEdge, ...).  No
// virtuals, never a base class.  A = start point, B = end point.
class phSegment {
public:
    enum {
        PROBE = 0,   // dominant: line-of-sight / ground / ledge probes
        EDGE  = 1    // water-surface edge test
    };

    Vector3 A, B;
    int Type;

    phSegment() : Type(PROBE) {}

    void Set(const Vector3 &start, const Vector3 &end, int type = PROBE) {
        A = start;
        B = end;
        Type = type;
    }
    void SetType(int type) { Type = type; }   // used when A/B are assigned separately
    void SetA(const Vector3 &start) { A = start; }
    void SetB(const Vector3 &end) { B = end; }
};

#endif // PHCORE_SEGMENT_H
