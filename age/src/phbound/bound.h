////////////////////////////////////////
// bound.h
////////////////////////////////////////

#ifndef PHBOUND_BOUND_H
#define PHBOUND_BOUND_H

#include "core/output.h"
#include "data/resource.h"
#include "vector/vector3.h"   // L9 -> L2, allowed
#include "vector/matrix34.h"
#include "atl/array.h"

// Feature flag the liquid subsystem (physics/forceliquid.h + rb's
// fzxWaterSurface) is gated on; the original build defined it here too.
#define BOUND_LIQUID 1

class phSegment;
class phIntersection;
class phMaterial;

class phBound {
public:
    enum eBoundType {
        COMPOSITE   = 1,   // phBoundComposite
        LIQUID      = 2,   // water/liquid volume
        HOTDOG      = 3,   // phBoundHotdog
        SPHERE      = 4,   // phBoundSphere
        BOX         = 5,
        GEOMETRY    = 6,
        POLYHEDRON  = 7,
        QUADTREE    = 8,
        OCTREE      = 9,
        OCTREEGRID  = 10,
        FORCESPHERE = 11,
        RIBBON      = 12
    };

    atArray<Vector3> Vertices;
    // Referenced materials. Owned by the material manager when one exists --
    // rbPhysMaterialMgr::Load returns pointers INTO its material map, so
    // deleting them here would free the map's entries out from under it
    // (double-free at ShutdownClass). Only materials in OwnedMaterials (the
    // no-manager fallback allocations) belong to this bound.
    int m_RefCount = 1;      // shared-bound reference count; see AddRef/Release
    atArray<phMaterial*> Materials;
    atArray<phMaterial*> OwnedMaterials;
    atArray<int> PolyMaterials;
    // Collision faces as a flat triangle list: 3 vertex indices (into Vertices)
    // per triangle. Quads from the .bnd are triangulated on load. Without this,
    // a loaded bound is just a point cloud and nothing can be collided against.
    atArray<int> TriVerts;
    Vector3 Centroid;
    float ActualRadius;

    // Placement of the bound relative to its instance, and the centre of
    // gravity the vehicle/traffic code steers (both in bound space).
    Vector3 Offset = Vector3(0.0f, 0.0f, 0.0f);
    Vector3 CGOffset = Vector3(0.0f, 0.0f, 0.0f);
    bool Forcefield = false;    // non-solid volume: reports contact, no collision response

    phBound();
    phBound(class datResource &rsc);
    virtual ~phBound();

    const Vector3 &GetOffset() const { return Offset; }
    void SetOffset(const Vector3 &o) { Offset = o; }
    const Vector3 &GetCGOffset() const { return CGOffset; }
    void SetCGOffset(const Vector3 &o) { CGOffset = o; }
    void GetCenterOfGravity(const class Matrix34 *m, Vector3 *cog) const;
    bool IsForcefield() const { return Forcefield; }
    void SetForcefield(bool on) { Forcefield = on; }
    // Radius that also covers the offset (sphere around the instance origin).
    float GetTotalRadius() const;
    // Copy the shared descriptive state (materials, offsets, extents) from
    // another bound; geometry stays with the source.
    virtual void Copy(const phBound *other);
    virtual int GetType() const;
    virtual bool IsPolygonal() const { return false; }

    virtual float GetActualRadius() const;
    virtual Vector3 GetBoxMin() const;
    virtual Vector3 GetBoxMax() const;
    virtual void GetBoxSize(Vector3 &out) const { out = GetBoxMax() - GetBoxMin(); }

    static phBound* Load(const char *name);
    static phBound* LoadFromFile(const char *name) { return Load(name); }
    static phBound* Load(class datTokenizer &tok);
    static phBound* Load(class datAsciiTokenizer &tok);
    static bool LoadHashTableExists(const char *name = nullptr);
    static void VirtualConstructFromPtr(class datResource &rsc, phBound *&bound);
    // Console page-in bodies also pass rvalues (GetArchetype()->GetBound()); those
    // construct in place on the console and are no-ops here (the PC builders of
    // the types involved read the bound themselves).
    template <typename T> static void VirtualConstructFromPtr(class datResource &, T) { Quitf("phBound::VirtualConstructFromPtr - not implemented"); }
    static void Explosion(const class Vector3 &pos, float force, float radius);

    // Bounds are shared: an archetype registered once by ARCHMGR is pointed at
    // by every instance that uses it, so the first user to let go must not take
    // the geometry away from the others.  A bound starts owned by whoever built
    // it, so the count starts at one and Release destroys it when the last
    // reference goes.  (phConfig::EnableRefCounting / FreezeRefCounting record
    // the game's intent for the archetype manager; the count here is always
    // maintained, because an unbalanced Release would be a use-after-free
    // whatever those flags say.)
    int GetRefCount() const { return m_RefCount; }
    void AddRef() { ++m_RefCount; }
    void Release() {
        if (m_RefCount > 0 && --m_RefCount > 0) return;
        delete this;
    }

    int GetNumMaterials() const { return Materials.GetCount(); }
    class phMaterial* GetMaterial(int index = 0) const { return (index >= 0 && index < Materials.GetCount()) ? Materials[index] : nullptr; }
    void SetMaterial(class phMaterial *m) { if (Materials.GetCount() > 0) Materials[0] = m; else Materials.Append(m); }
    void SetMaterial(const class phMaterial &m);

    class phMaterial* GetMaterialFromPartIndex(int index, int polyIndex = 0) const;

    // World-space point -> bound-local space (stub: identity until a transform exists).
    void UnTransform(const Vector3 &world, Vector3 &local) const;

    // Test a segment against this bound's edges; returns hit count (stub: none).
    int TestEdge(const phSegment &seg, phIntersection *isect, int maxHits) const;

    // Draw wireframe bound in world-space using transform matrix
    virtual void Draw(const Matrix34 &transform) const;

    // Editor trigger-volume display: tinted wireframe + transformed centroid.
    void DrawPhysics(const Matrix34 &transform, const Vector3 &color) const;
    Vector3 GetCenter(const Matrix34 *transform) const;

    // Test a point against this bound; returns true if point is inside.
    virtual bool TestPoint(const Vector3 &point, phIntersection *isect = nullptr) const;
};

class phBoundComposite : public phBound {
public:
    int NumBounds;
    phBound** Bounds;
    Matrix34* LocalMatrices;
    Matrix34* CurrentMatrices;
    Matrix34* LastMatrices;
    // The copy ctor shares the sub-bound pointers with the source composite
    // (rbPhysBoundComposite wraps the type's loaded bound per entity instance);
    // only the original owns and deletes them.
    bool OwnsBounds;

    phBoundComposite();
    explicit phBoundComposite(int numBounds);
    phBoundComposite(const phBoundComposite &other);
    virtual ~phBoundComposite();

    // (Re)allocate `numBounds` empty slots with identity local matrices.
    void Init(int numBounds);
    int GetNumBounds() const { return NumBounds; }
    int GetMaxNumBounds() const { return NumBounds; }
    int GetNumActiveBounds() const { return NumBounds; }
    phBound *GetBound(int i) const { return (i >= 0 && i < NumBounds && Bounds) ? Bounds[i] : nullptr; }
    // Slot assignment does not transfer ownership (the composite never
    // deletes a bound it was handed; it deletes only bounds it loaded).
    void SetBound(int i, phBound *bound);
    const Matrix34 &GetLocalMatrix(int i) const;
    void SetLocalMatrix(int i, const Matrix34 &m);
    const Matrix34 &GetCurrentMatrix(int i) const { return CurrentMatrices ? CurrentMatrices[i] : GetLocalMatrix(i); }
    // Recompute Centroid/ActualRadius from the sub-bounds and their local matrices.
    void CalcCenterOfBound();

    virtual int GetType() const override;
    virtual void Draw(const Matrix34 &transform) const override;
    class phMaterial* GetMaterialFromPartIndex(int index, int polyIndex = 0) const;
    virtual Vector3 GetBoxMin() const override;
    virtual Vector3 GetBoxMax() const override;
};

// Bounding box, centre and enclosing radius of a strided point array
// (traffic wheel-probe volumes).  Any output pointer may be null.
void ComputeBoundInfo(int numPoints, const float *points, int strideBytes, Vector3 *boxMin, Vector3 *boxMax, Vector3 *center, float *radius);

#endif // PHBOUND_BOUND_H
