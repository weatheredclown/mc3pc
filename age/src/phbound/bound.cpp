#include "phbound/bound.h"
#include "phbound/boundbox.h"
#include "phbound/boundrsc.h"
#include "phbound/boundsphere.h"
#include "phbound/boundhotdog.h"
#include "phcore/materialmgr.h"
#include "phcore/segment.h"
#include "phcore/isect.h"
#include "gfx/vgl.h"
#include "data/token.h"
#include "data/assetcfg.h"
#include "core/output.h"
#include <string>
#include <math.h>

phBound::phBound() : Centroid(0.0f, 0.0f, 0.0f), ActualRadius(0.0f) {}
phBound::phBound(datResource &rsc) : Centroid(0.0f, 0.0f, 0.0f), ActualRadius(0.0f) {}

phBound::~phBound() {
    // Materials handed out by the material manager stay alive in its map;
    // only the loader's fallback allocations are ours to free.
    for (int i = 0; i < OwnedMaterials.GetCount(); ++i) {
        delete OwnedMaterials[i];
    }
}

int phBound::GetType() const {
    return 0;
}

float phBound::GetActualRadius() const {
    return ActualRadius;
}

Vector3 phBound::GetBoxMin() const {
    if (Vertices.GetCount() > 0) {
        Vector3 mn(1e30f, 1e30f, 1e30f);
        for (int i = 0; i < Vertices.GetCount(); ++i) {
            const Vector3 &v = Vertices[i];
            if (v.x < mn.x) mn.x = v.x;
            if (v.y < mn.y) mn.y = v.y;
            if (v.z < mn.z) mn.z = v.z;
        }
        return mn;
    }
    float r = GetActualRadius();
    return Centroid - Vector3(r, r, r);
}

Vector3 phBound::GetBoxMax() const {
    if (Vertices.GetCount() > 0) {
        Vector3 mx(-1e30f, -1e30f, -1e30f);
        for (int i = 0; i < Vertices.GetCount(); ++i) {
            const Vector3 &v = Vertices[i];
            if (v.x > mx.x) mx.x = v.x;
            if (v.y > mx.y) mx.y = v.y;
            if (v.z > mx.z) mx.z = v.z;
        }
        return mx;
    }
    float r = GetActualRadius();
    return Centroid + Vector3(r, r, r);
}

void phBound::DrawPhysics(const Matrix34 &transform, const Vector3 &color) const {
    vglColor(mkfrgb(color.x, color.y, color.z));
    Draw(transform);
}

Vector3 phBound::GetCenter(const Matrix34 *transform) const {
    Vector3 c(Centroid);
    if (transform)
        transform->Transform(Centroid, c);
    return c;
}

// Stub: LoadHashTableExists is a legacy hook not used in this build, returns false.
bool phBound::LoadHashTableExists(const char *name) {
    return false;
}

// UnTransform maps world-space points to bound-local space by subtracting Centroid.
void phBound::UnTransform(const Vector3 &world, Vector3 &local) const {
    local.x = world.x - Centroid.x;
    local.y = world.y - Centroid.y;
    local.z = world.z - Centroid.z;
}

// TestEdge tests probe segments against the bound's bounding sphere.
int phBound::TestEdge(const phSegment &seg, phIntersection *isect, int maxHits) const {
    if (maxHits <= 0 || !isect) return 0;

    Vector3 d = seg.B - seg.A;
    float segLenSq = d.Mag2();
    if (segLenSq < 1e-8f) return 0;

    Vector3 f = seg.A - Centroid;
    float b = f.Dot(d);
    float c = f.Mag2() - (ActualRadius * ActualRadius);
    float discriminant = b * b - segLenSq * c;

    if (discriminant < 0.0f) return 0;

    discriminant = sqrtf(discriminant);
    float t1 = (-b - discriminant) / segLenSq;
    float t2 = (-b + discriminant) / segLenSq;

    float t = -1.0f;
    if (t1 >= 0.0f && t1 <= 1.0f) t = t1;
    else if (t2 >= 0.0f && t2 <= 1.0f) t = t2;

    if (t < 0.0f) return 0;

    Vector3 hitPos = seg.A + d * t;
    Vector3 hitNorm = hitPos - Centroid;
    float normLen = hitNorm.Mag();
    if (normLen > 1e-6f) hitNorm *= (1.0f / normLen);
    else hitNorm.Set(0.0f, 1.0f, 0.0f);

    float depth = ActualRadius - normLen;
    isect->Set(hitPos, hitNorm, t, depth > 0.0f ? depth : 0.0f, true, 0);
    return 1;
}

phMaterial* phBound::GetMaterialFromPartIndex(int index, int polyIndex) const {
    if (index >= 0 && index < PolyMaterials.GetCount()) {
        int mtlIdx = PolyMaterials[index];
        if (mtlIdx >= 0 && mtlIdx < Materials.GetCount()) {
            return Materials[mtlIdx];
        }
    }
    if (Materials.GetCount() > 0 && Materials[0]) {
        return Materials[0];
    }
    return nullptr;
}

bool phBound::TestPoint(const Vector3 &point, phIntersection *isect) const {
    float dist = Centroid.Dist(point);
    if (dist <= ActualRadius) {
        if (isect) {
            isect->Position = point;
            isect->Normal = (point - Centroid);
            isect->Normal.Normalize();
            isect->Depth = ActualRadius - dist;
            isect->Hit = true;
        }
        return true;
    }
    return false;
}

// Wireframe debug draws.  Compiled out for STANDALONE_TESTER targets
// (test_phbound/test_physics), which link no renderer backend.
#ifdef STANDALONE_TESTER
void phBound::Draw(const Matrix34 &) const {}
void phBoundSphere::Draw(const Matrix34 &) const {}
void phBoundHotdog::Draw(const Matrix34 &) const {}
#else
void phBound::Draw(const Matrix34 &transform) const {
    rglWorldMatrix(transform);
    int numTris = TriVerts.GetCount() / 3;
    if (numTris > 0 && Vertices.GetCount() > 0) {
        // PC port: Batch lines in chunks of up to 1024 triangles (6144 vertices) per draw call
        const int TRIS_PER_BATCH = 1024;
        for (int t = 0; t < numTris; t += TRIS_PER_BATCH) {
            int batchEnd = (t + TRIS_PER_BATCH < numTris) ? (t + TRIS_PER_BATCH) : numTris;
            int count = (batchEnd - t) * 6;
            rglBegin(drawLine, count);
            for (int i = t * 3; i < batchEnd * 3; i += 3) {
                int i0 = TriVerts[i], i1 = TriVerts[i + 1], i2 = TriVerts[i + 2];
                if (i0 < 0 || i1 < 0 || i2 < 0 ||
                    i0 >= Vertices.GetCount() || i1 >= Vertices.GetCount() || i2 >= Vertices.GetCount())
                    continue;
                const Vector3 &v0 = Vertices[i0];
                const Vector3 &v1 = Vertices[i1];
                const Vector3 &v2 = Vertices[i2];
                rglVertex3f(v0); rglVertex3f(v1);
                rglVertex3f(v1); rglVertex3f(v2);
                rglVertex3f(v2); rglVertex3f(v0);
            }
            rglEnd();
        }
    } else if (ActualRadius > 0.01f) {
        // PC port: Fallback wireframe circles for bounds without explicit triangle meshes
        const int SEGMENTS = 16;
        Vector3 c = Centroid;
        float r = ActualRadius;
        // XZ ring
        rglBegin(drawLineStrip, SEGMENTS + 1);
        for (int k = 0; k <= SEGMENTS; ++k) {
            float angle = k * (2.0f * 3.14159265f / SEGMENTS);
            rglVertex3f(c.x + cosf(angle) * r, c.y, c.z + sinf(angle) * r);
        }
        rglEnd();
        // XY ring
        rglBegin(drawLineStrip, SEGMENTS + 1);
        for (int k = 0; k <= SEGMENTS; ++k) {
            float angle = k * (2.0f * 3.14159265f / SEGMENTS);
            rglVertex3f(c.x + cosf(angle) * r, c.y + sinf(angle) * r, c.z);
        }
        rglEnd();
    }
}
#endif // !STANDALONE_TESTER

// --- phBoundSphere ---
phBoundSphere::phBoundSphere() : Radius(0.5f) { ActualRadius = 0.5f; }
phBoundSphere::phBoundSphere(float radius) : Radius(radius) { ActualRadius = radius; }
float phBoundSphere::GetRadius() const { return Radius; }
float phBoundSphere::GetActualRadius() const { return Radius; }

#ifndef STANDALONE_TESTER
void phBoundSphere::Draw(const Matrix34 &transform) const {
    rglWorldMatrix(transform);
    Vector3 c = Centroid;
    float r = Radius > 0.0f ? Radius : 1.0f;
    const int SEGMENTS = 16;
    // XZ ring
    rglBegin(drawLineStrip, SEGMENTS + 1);
    for (int k = 0; k <= SEGMENTS; ++k) {
        float angle = k * (2.0f * 3.14159265f / SEGMENTS);
        rglVertex3f(c.x + cosf(angle) * r, c.y, c.z + sinf(angle) * r);
    }
    rglEnd();
    // XY ring
    rglBegin(drawLineStrip, SEGMENTS + 1);
    for (int k = 0; k <= SEGMENTS; ++k) {
        float angle = k * (2.0f * 3.14159265f / SEGMENTS);
        rglVertex3f(c.x + cosf(angle) * r, c.y + sinf(angle) * r, c.z);
    }
    rglEnd();
    // YZ ring
    rglBegin(drawLineStrip, SEGMENTS + 1);
    for (int k = 0; k <= SEGMENTS; ++k) {
        float angle = k * (2.0f * 3.14159265f / SEGMENTS);
        rglVertex3f(c.x, c.y + cosf(angle) * r, c.z + sinf(angle) * r);
    }
    rglEnd();
}
#endif // !STANDALONE_TESTER

// --- phBoundHotdog ---
phBoundHotdog::phBoundHotdog() : Radius(0.5f), Length(1.0f) {}
phBoundHotdog::phBoundHotdog(float radius, float length) : Radius(radius), Length(length) {}
float phBoundHotdog::GetLength() const { return Length; }
float phBoundHotdog::GetRadius() const { return Radius; }
void phBoundHotdog::SetSize(float radius, float length) {
    Radius = radius;
    Length = length;
}
float phBoundHotdog::GetActualRadius() const { return Radius + Length * 0.5f; }

#ifndef STANDALONE_TESTER
void phBoundHotdog::Draw(const Matrix34 &transform) const {
    rglWorldMatrix(transform);
    Vector3 p1(0.0f, -Length * 0.5f, 0.0f);
    Vector3 p2(0.0f,  Length * 0.5f, 0.0f);
    float r = Radius;

    rglBegin(drawLine, 2);
    rglVertex3f(p1);
    rglVertex3f(p2);
    rglEnd();

    const int SEGMENTS = 12;
    rglBegin(drawLineStrip, SEGMENTS + 1);
    for (int k = 0; k <= SEGMENTS; ++k) {
        float angle = k * (2.0f * 3.14159265f / SEGMENTS);
        rglVertex3f(p1.x + cosf(angle) * r, p1.y, p1.z + sinf(angle) * r);
    }
    rglEnd();

    rglBegin(drawLineStrip, SEGMENTS + 1);
    for (int k = 0; k <= SEGMENTS; ++k) {
        float angle = k * (2.0f * 3.14159265f / SEGMENTS);
        rglVertex3f(p2.x + cosf(angle) * r, p2.y, p2.z + sinf(angle) * r);
    }
    rglEnd();
}
#endif // !STANDALONE_TESTER

// --- phBoundBox ---
phBoundBox::phBoundBox() : Size(1.0f, 1.0f, 1.0f) {
    CalculateExtents();
}

phBoundBox::phBoundBox(const Vector3 &size) : Size(size) {
    CalculateExtents();
}

void phBoundBox::SetSize(const Vector3 &size) {
    Size = size;
    CalculateExtents();
}

void phBoundBox::CalculateExtents() {
    Vector3 h = Size * 0.5f;
    ActualRadius = h.Mag();

    Vertices.Reset();
    TriVerts.Reset();
    PolyMaterials.Reset();

    Vertices.Append(Centroid + Vector3(-h.x, -h.y, -h.z)); // 0: ---
    Vertices.Append(Centroid + Vector3( h.x, -h.y, -h.z)); // 1: +--
    Vertices.Append(Centroid + Vector3( h.x,  h.y, -h.z)); // 2: ++-
    Vertices.Append(Centroid + Vector3(-h.x,  h.y, -h.z)); // 3: -+-
    Vertices.Append(Centroid + Vector3(-h.x, -h.y,  h.z)); // 4: --+
    Vertices.Append(Centroid + Vector3( h.x, -h.y,  h.z)); // 5: +-+
    Vertices.Append(Centroid + Vector3( h.x,  h.y,  h.z)); // 6: +++
    Vertices.Append(Centroid + Vector3(-h.x,  h.y,  h.z)); // 7: -++

    auto addQuad = [&](int i0, int i1, int i2, int i3, const Vector3 &expectedNormal) {
        Vector3 edge1 = Vertices[i1] - Vertices[i0];
        Vector3 edge2 = Vertices[i2] - Vertices[i0];
        Vector3 n;
        n.Cross(edge1, edge2);
        if (n.Dot(expectedNormal) < 0.0f) {
            int tmp = i1; i1 = i2; i2 = tmp;
        }
        TriVerts.Append(i0); TriVerts.Append(i1); TriVerts.Append(i2);
        TriVerts.Append(i0); TriVerts.Append(i2); TriVerts.Append(i3);
        PolyMaterials.Append(0);
        PolyMaterials.Append(0);
    };

    // Front (+Z)
    addQuad(4, 5, 6, 7, Vector3(0.0f, 0.0f, 1.0f));
    // Back (-Z)
    addQuad(1, 0, 3, 2, Vector3(0.0f, 0.0f, -1.0f));
    // Top (+Y)
    addQuad(7, 6, 2, 3, Vector3(0.0f, 1.0f, 0.0f));
    // Bottom (-Y)
    addQuad(0, 1, 5, 4, Vector3(0.0f, -1.0f, 0.0f));
    // Right (+X)
    addQuad(5, 1, 2, 6, Vector3(1.0f, 0.0f, 0.0f));
    // Left (-X)
    addQuad(0, 4, 7, 3, Vector3(-1.0f, 0.0f, 0.0f));
}

phBoundComposite::phBoundComposite()
    : NumBounds(0), Bounds(nullptr), LocalMatrices(nullptr), CurrentMatrices(nullptr), LastMatrices(nullptr),
      OwnsBounds(true) {}

int phBoundComposite::GetType() const {
    return COMPOSITE;
}

void phBoundComposite::Draw(const Matrix34 &transform) const {
    for (int i = 0; i < NumBounds; ++i) {
        if (!Bounds[i]) continue;
        Matrix34 subM = transform;
        if (LocalMatrices) {
            subM.Dot(LocalMatrices[i], transform);
        }
        Bounds[i]->Draw(subM);
    }
}

phBoundComposite::phBoundComposite(const phBoundComposite &other) {
    NumBounds = other.NumBounds;
    if (NumBounds > 0) {
        Bounds = new phBound*[NumBounds];
        for (int i = 0; i < NumBounds; ++i) {
            Bounds[i] = other.Bounds[i];
        }
    } else {
        Bounds = nullptr;
    }
    // Sub-bounds are shared with (and owned by) the source composite.
    OwnsBounds = false;
    Centroid = other.Centroid;
    ActualRadius = other.ActualRadius;
    LocalMatrices = nullptr;
    CurrentMatrices = nullptr;
    LastMatrices = nullptr;
}

phBoundComposite::~phBoundComposite() {
    if (Bounds) {
        if (OwnsBounds) {
            for (int i = 0; i < NumBounds; ++i) {
                delete Bounds[i];
            }
        }
        delete[] Bounds;
    }
    if (LocalMatrices) {
        delete[] LocalMatrices;
    }
}

phMaterial* phBoundComposite::GetMaterialFromPartIndex(int index, int polyIndex) const {
    if (polyIndex >= 0 && polyIndex < NumBounds && Bounds[polyIndex]) {
        return Bounds[polyIndex]->GetMaterialFromPartIndex(index, 0);
    }
    return nullptr;
}

Vector3 phBoundComposite::GetBoxMin() const {
    Vector3 mn(1e30f, 1e30f, 1e30f);
    bool hasAny = false;
    for (int i = 0; i < NumBounds; ++i) {
        if (!Bounds || !Bounds[i]) continue;
        const Matrix34 &m = (LocalMatrices && i < NumBounds) ? LocalMatrices[i] : Matrix34::I;
        Vector3 subMin = Bounds[i]->GetBoxMin();
        Vector3 subMax = Bounds[i]->GetBoxMax();
        for (int c = 0; c < 8; ++c) {
            Vector3 corner((c & 1) ? subMax.x : subMin.x,
                           (c & 2) ? subMax.y : subMin.y,
                           (c & 4) ? subMax.z : subMin.z);
            Vector3 w; m.Transform(corner, w);
            if (w.x < mn.x) mn.x = w.x;
            if (w.y < mn.y) mn.y = w.y;
            if (w.z < mn.z) mn.z = w.z;
        }
        hasAny = true;
    }
    if (hasAny) return mn;
    return phBound::GetBoxMin();
}

Vector3 phBoundComposite::GetBoxMax() const {
    Vector3 mx(-1e30f, -1e30f, -1e30f);
    bool hasAny = false;
    for (int i = 0; i < NumBounds; ++i) {
        if (!Bounds || !Bounds[i]) continue;
        const Matrix34 &m = (LocalMatrices && i < NumBounds) ? LocalMatrices[i] : Matrix34::I;
        Vector3 subMin = Bounds[i]->GetBoxMin();
        Vector3 subMax = Bounds[i]->GetBoxMax();
        for (int c = 0; c < 8; ++c) {
            Vector3 corner((c & 1) ? subMax.x : subMin.x,
                           (c & 2) ? subMax.y : subMin.y,
                           (c & 4) ? subMax.z : subMin.z);
            Vector3 w; m.Transform(corner, w);
            if (w.x > mx.x) mx.x = w.x;
            if (w.y > mx.y) mx.y = w.y;
            if (w.z > mx.z) mx.z = w.z;
        }
        hasAny = true;
    }
    if (hasAny) return mx;
    return phBound::GetBoxMax();
}

phBound* phBound::Load(const char *name) {
    Stream *s = ASSET.Open(name, "bnd");
    if (!s) {
        s = ASSET.Open(name, "bound");
    }
    if (!s) {
        char sub[256];
        snprintf(sub, sizeof(sub), "bound/%s", name);
        s = ASSET.Open(sub, "bnd");
    }
    if (!s) {
        char sub[256];
        snprintf(sub, sizeof(sub), "%s/%s", name, name);
        s = ASSET.Open(sub, "bnd");
    }
    if (!s) {
        // Return a default sphere to prevent crashing
        return new phBoundSphere();
    }
    datTokenizer tok;
    tok.Init(name, s);
    phBound *b = phBound::Load(tok);
    s->Close();
    return b;
}

phBound* phBound::Load(datTokenizer &tok) {
    // .bnd is a strict, fixed-order text format (see any Entity/*/Bound.bnd).
    // Read it in that order with the tokenizer's normal read calls -- do NOT peek
    // with GetCurrentToken() before a read: datAsciiTokenizer defers its very
    // first token (so a loader can set CommentChar after Init()), so an un-primed
    // peek returns "" and would silently abort the whole parse.
    //
    // Layout:
    //   version: <float>
    //   type: <bound-type>                       e.g. "geometry"
    //   verts: <N>
    //   [centroid: <x y z>]  [cg: <x y z>]       (omitted by some bounds)
    //   materials: <M>   edges: <E>   polys: <P>   [readedgenormals: <i>]
    //   N x  "v <x y z>"
    //   M x  "[type: <t>] mtl <name> { ... }"
    //   E x  "edge <a> <b> <nx> <ny> <nz>"
    //   P x  "quad <v0 v1 v2 v3> <mtl> <e0 e1 e2 e3>"
    //        "tri  <v0 v1 v2>    <mtl> <e0 e1 e2>"
    char buf[256];
    buf[0] = '\0';

    if (tok.CheckToken("version:")) tok.GetFloat();

    if (tok.CheckToken("type:")) tok.GetToken(buf, sizeof(buf));  // bound type, e.g. "geometry"

    // Composite bounds (e.g. Entity/IAControlDoor/Bound.bnd) wrap N sub-bounds,
    // each a regular bound block followed by a 4x3 "matrix:" placing it in the
    // composite's frame.  At runtime rbPhysBoundComposite overwrites
    // LocalMatrices from the entity's skeleton so animated parts (door panels)
    // carry their bounds with them.
    //
    //   type: composite
    //   numBounds: <N>
    //   centroid: <x y z>
    //   N x { "bound: <i>"  <regular bound text>  "matrix:" <3x3 rows + trans row> }
    if (strcmp(buf, "composite") == 0) {
        phBoundComposite *comp = new phBoundComposite();
        int numBounds = 0;
        if (tok.CheckToken("numBounds:")) numBounds = tok.GetInt();
        if (tok.CheckToken("centroid:")) tok.GetVector(comp->Centroid);
        if (numBounds <= 0) {
            delete comp;
            return new phBoundSphere();
        }

        comp->NumBounds = numBounds;
        comp->Bounds = new phBound*[numBounds];
        comp->LocalMatrices = new Matrix34[numBounds];
        for (int i = 0; i < numBounds; ++i) {
            comp->Bounds[i] = nullptr;
            comp->LocalMatrices[i].Identity();
        }
        comp->CurrentMatrices = comp->LocalMatrices;
        comp->LastMatrices = comp->LocalMatrices;

        for (int i = 0; i < numBounds; ++i) {
            if (tok.CheckToken("bound:")) tok.GetInt();   // sub-bound index
            comp->Bounds[i] = phBound::Load(tok);         // regular bound block
            if (tok.CheckToken("matrix:")) {              // 3 rotation rows + translation row
                Matrix34 &m = comp->LocalMatrices[i];
                tok.GetVector(m.a);
                tok.GetVector(m.b);
                tok.GetVector(m.c);
                tok.GetVector(m.d);
            }
        }

        // Enclosing-sphere radius over the placed sub-bounds.
        float maxR = 0.0f;
        for (int i = 0; i < numBounds; ++i) {
            if (!comp->Bounds[i]) continue;
            Vector3 c;
            comp->LocalMatrices[i].Transform(comp->Bounds[i]->Centroid, c);
            float r = (c - comp->Centroid).Mag() + comp->Bounds[i]->GetActualRadius();
            if (r > maxR) maxR = r;
        }
        comp->ActualRadius = (maxR > 0.0f) ? maxR : 0.5f;

        return comp;
    }

    if (strcmp(buf, "box") == 0) {
        phBoundBox *box = new phBoundBox();
        if (tok.CheckToken("size:")) tok.GetVector(box->Size);
        if (tok.CheckToken("centroid:")) tok.GetVector(box->Centroid);
        if (tok.CheckToken("cg:")) { Vector3 cg; tok.GetVector(cg); }
        int nMaterials = 0;
        if (tok.CheckToken("materials:")) nMaterials = tok.GetInt();
        for (int i = 0; i < nMaterials; ++i) {
            phMaterial *mat = nullptr;
            if (phMaterialMgr::Instance) {
                mat = phMaterialMgr::Instance->Load(&tok);
            }
            if (!mat) {
                mat = new phMaterial();
                if (tok.CheckToken("type:")) tok.GetToken(buf, sizeof(buf));
                if (tok.CheckToken("mtl")) {
                    mat->LoadData(tok);
                }
                box->OwnedMaterials.Append(mat);
            }
            box->Materials.Append(mat);
        }
        box->CalculateExtents();
        return box;
    }

    phBound *b = new phBound();

    int nVerts = 0, nMaterials = 0, nEdges = 0, nPolys = 0;
    if (tok.CheckToken("verts:"))           nVerts = tok.GetInt();
    if (tok.CheckToken("centroid:"))        tok.GetVector(b->Centroid);
    if (tok.CheckToken("cg:"))              { Vector3 cg; tok.GetVector(cg); }
    if (tok.CheckToken("materials:"))       nMaterials = tok.GetInt();
    if (tok.CheckToken("edges:"))           nEdges = tok.GetInt();
    if (tok.CheckToken("polys:"))           nPolys = tok.GetInt();
    if (tok.CheckToken("readedgenormals:")) tok.GetInt();

    // Vertices.
    for (int i = 0; i < nVerts; ++i) {
        if (!tok.CheckToken("v")) break;
        Vector3 v;
        tok.GetVector(v);
        b->Vertices.Append(v);
    }

    // Physics-material blocks.
    for (int i = 0; i < nMaterials; ++i) {
        phMaterial *mat = nullptr;
        if (phMaterialMgr::Instance) {
            mat = phMaterialMgr::Instance->Load(&tok);
        }
        if (!mat) {
            mat = new phMaterial();
            if (tok.CheckToken("type:")) tok.GetToken(buf, sizeof(buf));
            if (tok.CheckToken("mtl")) {
                mat->LoadData(tok);
            }
            b->OwnedMaterials.Append(mat);
        }
        b->Materials.Append(mat);
    }

    // Edges ("edge a b nx ny nz") -- not needed for collision; consume and drop.
    for (int i = 0; i < nEdges; ++i) {
        if (!tok.CheckToken("edge")) break;
        tok.GetInt(); tok.GetInt();
        tok.GetFloat(); tok.GetFloat(); tok.GetFloat();
    }

    // Polygons -> collision triangles. Each poly is: <N vertex indices> <material
    // index> <N edge indices>. Quads are split into two triangles.
    for (int i = 0; i < nPolys; ++i) {
        if (tok.CheckToken("quad")) {
            int v0 = tok.GetInt(), v1 = tok.GetInt(), v2 = tok.GetInt(), v3 = tok.GetInt();
            int mtl = tok.GetInt();
            b->PolyMaterials.Append(mtl);
            b->PolyMaterials.Append(mtl);                             // 2 triangles per quad
            tok.GetInt(); tok.GetInt(); tok.GetInt(); tok.GetInt();   // 4 edge indices
            b->TriVerts.Append(v0); b->TriVerts.Append(v1); b->TriVerts.Append(v2);
            b->TriVerts.Append(v0); b->TriVerts.Append(v2); b->TriVerts.Append(v3);
        } else if (tok.CheckToken("tri")) {
            int v0 = tok.GetInt(), v1 = tok.GetInt(), v2 = tok.GetInt();
            int mtl = tok.GetInt();
            b->PolyMaterials.Append(mtl);                             // 1 triangle per tri
            tok.GetInt(); tok.GetInt(); tok.GetInt();                 // 3 edge indices
            b->TriVerts.Append(v0); b->TriVerts.Append(v1); b->TriVerts.Append(v2);
        } else {
            break;
        }
    }

    // A file with no geometry (or an unrecognized layout) falls back to a unit
    // sphere so callers always get a usable bound.
    if (b->Vertices.GetCount() == 0) {
        delete b;
        return new phBoundSphere();
    }

    // Enclosing-sphere radius about the centroid.
    float maxDistSq = 0.0f;
    for (int i = 0; i < b->Vertices.GetCount(); ++i) {
        float distSq = (b->Vertices[i] - b->Centroid).Mag2();
        if (distSq > maxDistSq) maxDistSq = distSq;
    }
    b->ActualRadius = (maxDistSq > 0.0f) ? sqrtf(maxDistSq) : 0.5f;

    return b;
}

phBound* phBound::Load(datAsciiTokenizer &tok) {
    return Load(tok);
}

void phBound::VirtualConstructFromPtr(class datResource &rsc, phBound *&bound) {
    // `bound` carries the image address left by rsc.PointerFixup (data/resource.h);
    // the bound is built from its place in the pack image (phbound/boundrsc.h).
    u32 addr = rsc.AddressOf(bound);
    if (addr) {
        if (!rsc.GetImage()) {
            Quitf("phBound::VirtualConstructFromPtr: bound at %08x has no image", addr);
        }
        bound = phBoundLoadFromResource(*rsc.GetImage(), addr);
        if (!bound) {
            Quitf("phBound::VirtualConstructFromPtr: failed to load bound at %08x from '%s'", addr, rsc.GetImage()->GetName());
        }
    } else {
        bound = 0;
    }
}

void phBound::Explosion(const Vector3 &pos, float force, float radius) {
}

void phBound::SetMaterial(const phMaterial &m) {
    phMaterial *mat = const_cast<phMaterial *>(&m);
    if (Materials.GetCount() > 0) {
        Materials[0] = mat;
    } else {
        Materials.Append(mat);
    }
}

////////////////////////////////////////////////////////////////////////////////
// Offsets, composite slots and point-cloud extents (AGE 2.72 surface).

float phBound::GetTotalRadius() const {
    return ActualRadius + Offset.Mag();
}

void phBound::Copy(const phBound *other) {
    if (!other || other == this) return;
    Materials = other->Materials;
    PolyMaterials = other->PolyMaterials;
    Centroid = other->Centroid;
    ActualRadius = other->ActualRadius;
    Offset = other->Offset;
    CGOffset = other->CGOffset;
    Forcefield = other->Forcefield;
}

phBoundComposite::phBoundComposite(int numBounds)
    : NumBounds(0), Bounds(nullptr), LocalMatrices(nullptr), CurrentMatrices(nullptr), LastMatrices(nullptr),
      OwnsBounds(false) {
    Init(numBounds);
}

void phBoundComposite::Init(int numBounds) {
    if (Bounds) {
        if (OwnsBounds) {
            for (int i = 0; i < NumBounds; ++i) delete Bounds[i];
        }
        delete[] Bounds;
    }
    delete[] LocalMatrices;
    delete[] CurrentMatrices;
    delete[] LastMatrices;
    NumBounds = numBounds > 0 ? numBounds : 0;
    Bounds = NumBounds ? new phBound*[NumBounds] : nullptr;
    LocalMatrices = NumBounds ? new Matrix34[NumBounds] : nullptr;
    CurrentMatrices = NumBounds ? new Matrix34[NumBounds] : nullptr;
    LastMatrices = NumBounds ? new Matrix34[NumBounds] : nullptr;
    for (int i = 0; i < NumBounds; ++i) {
        Bounds[i] = nullptr;
        LocalMatrices[i].Identity();
        CurrentMatrices[i].Identity();
        LastMatrices[i].Identity();
    }
    OwnsBounds = false;   // slots filled through SetBound are borrowed
}

void phBoundComposite::SetBound(int i, phBound *bound) {
    if (i < 0 || i >= NumBounds || !Bounds) return;
    if (OwnsBounds && Bounds[i] && Bounds[i] != bound) delete Bounds[i];
    Bounds[i] = bound;
}

static const Matrix34 sIdentityMatrix = []() { Matrix34 m; m.Identity(); return m; }();

const Matrix34 &phBoundComposite::GetLocalMatrix(int i) const {
    if (i < 0 || i >= NumBounds || !LocalMatrices) return sIdentityMatrix;
    return LocalMatrices[i];
}

void phBoundComposite::SetLocalMatrix(int i, const Matrix34 &m) {
    if (i < 0 || i >= NumBounds) return;
    if (!LocalMatrices) {
        LocalMatrices = new Matrix34[NumBounds];
        for (int k = 0; k < NumBounds; ++k) LocalMatrices[k].Identity();
    }
    if (!CurrentMatrices) {
        CurrentMatrices = new Matrix34[NumBounds];
        for (int k = 0; k < NumBounds; ++k) CurrentMatrices[k].Identity();
    }
    LocalMatrices[i] = m;
    CurrentMatrices[i] = m;
}

void phBoundComposite::CalcCenterOfBound() {
    Vector3 lo(0.0f, 0.0f, 0.0f), hi(0.0f, 0.0f, 0.0f);
    bool any = false;
    for (int i = 0; i < NumBounds; ++i) {
        if (!Bounds || !Bounds[i]) continue;
        const Matrix34 &m = GetLocalMatrix(i);
        Vector3 c;
        m.Transform(Bounds[i]->Centroid + Bounds[i]->Offset, c);
        float r = Bounds[i]->ActualRadius;
        Vector3 bmin(c.x - r, c.y - r, c.z - r), bmax(c.x + r, c.y + r, c.z + r);
        if (!any) { lo = bmin; hi = bmax; any = true; }
        else { lo.Min(bmin); hi.Max(bmax); }
    }
    if (!any) return;
    Centroid = (lo + hi) * 0.5f;
    ActualRadius = (hi - lo).Mag() * 0.5f;
}

void ComputeBoundInfo(int numPoints, const float *points, int strideBytes, Vector3 *boxMin, Vector3 *boxMax, Vector3 *center, float *radius) {
    Vector3 lo(0.0f, 0.0f, 0.0f), hi(0.0f, 0.0f, 0.0f);
    if (strideBytes <= 0) strideBytes = (int)sizeof(Vector3);
    const unsigned char *p = (const unsigned char *)points;
    for (int i = 0; i < numPoints; ++i, p += strideBytes) {
        const float *f = (const float *)p;
        Vector3 v(f[0], f[1], f[2]);
        if (i == 0) { lo = v; hi = v; }
        else { lo.Min(v); hi.Max(v); }
    }
    Vector3 c = (lo + hi) * 0.5f;
    float r = 0.0f;
    p = (const unsigned char *)points;
    for (int i = 0; i < numPoints; ++i, p += strideBytes) {
        const float *f = (const float *)p;
        float d = Vector3(f[0] - c.x, f[1] - c.y, f[2] - c.z).Mag();
        if (d > r) r = d;
    }
    if (boxMin) *boxMin = lo;
    if (boxMax) *boxMax = hi;
    if (center) *center = c;
    if (radius) *radius = r;
}

void phBound::GetCenterOfGravity(const Matrix34 *m, Vector3 *cog) const {
    if (!cog) return;
    Vector3 local = Offset + CGOffset;
    if (m) {
        m->Transform(local, *cog);
    } else {
        *cog = local;
    }
}
