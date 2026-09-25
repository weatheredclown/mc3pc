#ifndef MESH_MESH_H
#define MESH_MESH_H

#include "vector/vector3.h"
#include "vector/vector2.h"
#include "vector/Vector4.h"
#include "mesh/array.h"
#include <string>

typedef unsigned short mshIndex;

enum {
    mshTRIANGLES,
    mshTRISTRIP,
    mshQUADS,
    mshPOLYGON,
    // A strip whose first triangle winds the other way: the original splits a
    // strip after an odd triangle into a TRISTRIP2 (mcgfx/vcache_material.c).
    mshTRISTRIP2
};

class mshPrimitive {
public:
    int Type;
    int Priority;
    mshArray<mshIndex> Idx;

    mshPrimitive() : Type(mshTRIANGLES), Priority(0) {}
    int GetSubPrimitiveCount() const { return Type == mshTRIANGLES ? Idx.GetCount() / 3 : Idx.GetCount(); }
};

class mshMaterial {
public:
    std::string Name;
    int Priority;
    mshArray<mshPrimitive> Prim;

    mshMaterial() : Priority(0) {}
};

// The editor-side mesh: positions / normals / colours / two UV sets plus an
// "adjunct" table that indexes into them (one adjunct per unique vertex
// tuple), and materials whose primitives index the adjuncts.  Read from the
// ascii ".mesh" serialisation (see mesh.cpp) and converted to a gfxModel by
// gfxModelFromMesh().
class mshMesh {
public:
    struct AdjInfo {
        int P;      // position
        int N;      // normal (-1 none)
        int C0;     // colour (-1 none)
        int T0;     // uv set 0 (-1 none)
        int T1;     // uv set 1 (-1 none)
        AdjInfo() : P(0), N(-1), C0(-1), T0(-1), T1(-1) {}
    };

    mshArray<mshMaterial> m_Materials;
    mshArray<Vector3> m_Positions;
    mshArray<Vector3> m_Normals;
    mshArray<Vector4> m_Colors;
    mshArray<Vector2> m_Tex0;
    mshArray<Vector2> m_Tex1;
    mshArray<AdjInfo> m_Adjs;

    mshMesh() {}
    virtual ~mshMesh() {}

    int GetMtlCount() const { return m_Materials.GetCount(); }
    mshMaterial & GetMtl(int i) { return m_Materials[i]; }
    const mshMaterial & GetMtl(int i) const { return m_Materials[i]; }
    void AddMtl(const mshMaterial &mtl) { m_Materials.Append(mtl); }

    Vector3 & GetPos(int i) { return m_Positions[i]; }
    const Vector3 & GetPos(int i) const { return m_Positions[i]; }

    AdjInfo & GetAdj(int i) { return m_Adjs[i]; }
    const AdjInfo & GetAdj(int i) const { return m_Adjs[i]; }

    void Reset() {
        m_Materials.Reset();
        m_Positions.Reset();
        m_Normals.Reset();
        m_Colors.Reset();
        m_Tex0.Reset();
        m_Tex1.Reset();
        m_Adjs.Reset();
    }

    mshIndex AddAdj(const Vector3 &pos, int a, const Vector4* color, int b, int c) {
        m_Positions.Append(pos);
        AdjInfo info;
        info.P = m_Positions.GetCount() - 1;
        if (color) { m_Colors.Append(*color); info.C0 = m_Colors.GetCount() - 1; }
        m_Adjs.Append(info);
        return (mshIndex)(m_Adjs.GetCount() - 1);
    }

    // Load the ascii v1.10 ".mod" as a mesh (positions/normals/colours/uvs +
    // one material per .mod material).
    bool LoadMod(const char* filename);
    bool SaveMod(const char* filename, bool arg1 = false, int arg2 = 0, int arg3 = 0, bool arg4 = false) { return true; }
    // Immediate-mode debug draw of every triangle with the current RSTATE.
    void Draw() const;
    template <typename T>
    void Draw(const T &arg) const { Draw(); }
    void Serialize(class mshSerializer &ser);
};

#endif // MESH_MESH_H
