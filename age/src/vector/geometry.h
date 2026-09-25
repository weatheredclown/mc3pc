#ifndef VECTOR_GEOMETRY_H
#define VECTOR_GEOMETRY_H

#include "vector/vector3.h"
#include <math.h>

inline bool IsPointBehindPlane(const Vector3 &pt, const Vector3 &p, const Vector3 &n)
{
    return (pt - p).Dot(n) <= 0.0f;
}

inline bool Test2DPointVsTri(const Vector3 &pt, const Vector3 &p0, const Vector3 &p1, const Vector3 &p2)
{
    float c1 = (p1.x - p0.x) * (pt.z - p0.z) - (p1.z - p0.z) * (pt.x - p0.x);
    float c2 = (p2.x - p1.x) * (pt.z - p1.z) - (p2.z - p1.z) * (pt.x - p1.x);
    float c3 = (p0.x - p2.x) * (pt.z - p2.z) - (p0.z - p2.z) * (pt.x - p2.x);
    return (c1 >= 0.0f && c2 >= 0.0f && c3 >= 0.0f) || (c1 <= 0.0f && c2 <= 0.0f && c3 <= 0.0f);
}

// Intersect the segment start + t*edge (t in [0,1]) with an axis-aligned box
// centered at the origin with the given half-extents (slab method).  Returns
// the number of boundary crossings (0..2); t1/normal1/index1 hold the first
// crossing, t2/normal2/index2 the second.  Normals are outward face normals;
// index encodes the face as axis*2 + (positive face ? 1 : 0).  Used by
// fzxWaterSurface::TestEdge/TestProbe against its own water volume.
inline int SegmentToBoxIntersections(const Vector3 &start, const Vector3 &edge,
                                     float halfX, float halfY, float halfZ,
                                     float *t1, float *t2,
                                     Vector3 *normal1, Vector3 *normal2,
                                     int *index1, int *index2)
{
    const float half[3] = { halfX, halfY, halfZ };
    const float s[3] = { start.x, start.y, start.z };
    const float d[3] = { edge.x, edge.y, edge.z };

    float tEnter = 0.0f, tExit = 1.0f;
    int enterAxis = -1, exitAxis = -1;

    for (int a = 0; a < 3; a++) {
        if (fabsf(d[a]) < 1e-12f) {
            if (s[a] < -half[a] || s[a] > half[a]) return 0;
            continue;
        }
        float inv = 1.0f / d[a];
        float tNear = (-half[a] - s[a]) * inv;
        float tFar  = ( half[a] - s[a]) * inv;
        if (tNear > tFar) { float tmp = tNear; tNear = tFar; tFar = tmp; }
        if (tNear > tEnter) { tEnter = tNear; enterAxis = a; }
        if (tFar < tExit)   { tExit = tFar; exitAxis = a; }
        if (tEnter > tExit) return 0;
    }

    int hits = 0;

    // Entry crossing (skipped when the segment starts inside the box).
    if (enterAxis >= 0 && tEnter > 0.0f) {
        Vector3 n(0.0f, 0.0f, 0.0f);
        bool positiveFace = (d[enterAxis] < 0.0f);
        (&n.x)[enterAxis] = positiveFace ? 1.0f : -1.0f;
        if (t1) *t1 = tEnter;
        if (normal1) *normal1 = n;
        if (index1) *index1 = enterAxis * 2 + (positiveFace ? 1 : 0);
        hits = 1;
    }

    // Exit crossing (skipped when the segment ends inside the box).
    if (exitAxis >= 0 && tExit < 1.0f && tExit > tEnter) {
        Vector3 n(0.0f, 0.0f, 0.0f);
        bool positiveFace = (d[exitAxis] > 0.0f);
        (&n.x)[exitAxis] = positiveFace ? 1.0f : -1.0f;
        float tVal = tExit;
        int faceIndex = exitAxis * 2 + (positiveFace ? 1 : 0);
        if (hits == 0) {
            if (t1) *t1 = tVal;
            if (normal1) *normal1 = n;
            if (index1) *index1 = faceIndex;
        } else {
            if (t2) *t2 = tVal;
            if (normal2) *normal2 = n;
            if (index2) *index2 = faceIndex;
        }
        hits++;
    }

    return hits;
}

inline int SegmentToBoxIntersections(const Vector3 &start, const Vector3 &edge,
                                     const Vector3 &half,
                                     float *t1, float *t2,
                                     Vector3 *normal1, Vector3 *normal2,
                                     int *index1, int *index2)
{
    return SegmentToBoxIntersections(start, edge, half.x, half.y, half.z,
                                     t1, t2, normal1, normal2, index1, index2);
}

inline float FindTValueOpenSegToPoint(const Vector3 &p0, const Vector3 &d, const Vector3 &p)
{
    float d2 = d.Mag2();
    if (d2 < 1e-12f) return 0.0f;
    return (p - p0).Dot(d) / d2;
}

inline float Distance2LineToPoint(const Vector3 &p0, const Vector3 &d, const Vector3 &p)
{
    float t = FindTValueOpenSegToPoint(p0, d, p);
    Vector3 closest = p0 + d * t;
    return (p - closest).Mag2();
}

inline float FindClosestPointSegToPoint(const Vector3 &a, const Vector3 &ab, const Vector3 &p, Vector3 &closest)
{
    float t = FindTValueOpenSegToPoint(a, ab, p);
    if (t < 0.0f) t = 0.0f;
    else if (t > 1.0f) t = 1.0f;
    closest = a + ab * t;
    return t;
}

// Squared distance from p to the segment a..a+ab.  The second argument is the
// segment VECTOR, not its end point: the alpha (0x230e68) passes it straight to
// FindClosestPointSegToPoint, and every caller (the AI steering-sphere pass test
// in mcai/state.cpp, steersource.cpp, mcped) passes next-minus-first.  Treating
// it as an end point measured against the wrong segment, so AI opponents rarely
// registered passing a steering sphere (race-line fallback spam, recovery teleports).
inline float Distance2SegToPoint(const Vector3 &a, const Vector3 &ab, const Vector3 &p)
{
    Vector3 closest;
    FindClosestPointSegToPoint(a, ab, p, closest);
    return (p - closest).Mag2();
}

inline bool Test2DSegVsSeg(float x1, float y1, float x2, float y2, float &t1,
                           float x3, float y3, float x4, float y4, float &t2,
                           bool directed = false)
{
    float dx1 = x2 - x1;
    float dy1 = y2 - y1;
    float dx2 = x4 - x3;
    float dy2 = y4 - y3;

    float det = dx1 * dy2 - dy1 * dx2;
    if (directed) {
        if (det <= 1e-6f) return false;
    } else {
        if (fabsf(det) < 1e-6f) return false;
    }

    float invDet = 1.0f / det;
    float dx31 = x3 - x1;
    float dy31 = y3 - y1;

    float s = (dx31 * dy2 - dy31 * dx2) * invDet;
    float t = (dx31 * dy1 - dy31 * dx1) * invDet;

    if (s >= 0.0f && s <= 1.0f && t >= 0.0f && t <= 1.0f) {
        t1 = s;
        t2 = t;
        return true;
    }
    return false;
}

// Overlap test between two oriented rectangles in the XZ plane, each given
// by centre, facing direction (its length axis), half length and half width.
// Separating-axis test over the four edge directions.
inline bool RectangleTouchesRectangle(const Vector3 &posA, const Vector3 &dirA, float halfLenA, float halfWidthA,
                                      const Vector3 &posB, const Vector3 &dirB, float halfLenB, float halfWidthB)
{
	// Unit length axes and their perpendiculars, flattened to XZ.
	float lenA = sqrtf(dirA.x * dirA.x + dirA.z * dirA.z);
	float lenB = sqrtf(dirB.x * dirB.x + dirB.z * dirB.z);
	float ax = lenA > 1e-6f ? dirA.x / lenA : 0.0f, az = lenA > 1e-6f ? dirA.z / lenA : 1.0f;
	float bx = lenB > 1e-6f ? dirB.x / lenB : 0.0f, bz = lenB > 1e-6f ? dirB.z / lenB : 1.0f;
	float axes[4][2] = {{ax, az}, {-az, ax}, {bx, bz}, {-bz, bx}};
	float dx = posB.x - posA.x, dz = posB.z - posA.z;
	for (int i = 0; i < 4; i++) {
		float nx = axes[i][0], nz = axes[i][1];
		float ra = fabsf(nx * ax + nz * az) * halfLenA + fabsf(nx * -az + nz * ax) * halfWidthA;
		float rb = fabsf(nx * bx + nz * bz) * halfLenB + fabsf(nx * -bz + nz * bx) * halfWidthB;
		float d = fabsf(nx * dx + nz * dz);
		if (d > ra + rb) return false;
	}
	return true;
}

#endif // VECTOR_GEOMETRY_H
