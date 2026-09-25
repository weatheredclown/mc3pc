////////////////////////////////////////
// mesh.cpp -- ascii ".mesh" reader, .mod import, debug draw
////////////////////////////////////////

#include "mesh/mesh.h"
#include "mesh/serialize.h"
#include "data/assetcfg.h"
#include "data/token.h"
#include "core/stream.h"
#include "core/output.h"
#include "gfx/model.h"
#include "gfx/vgl.h"
#include <string.h>

static void sSkipBlock(datTokenizer &tok)
{
    // consumes tokens until the matching '}' (assumes '{' already read)
    char t[256]; int depth = 1;
    while (depth > 0 && tok.GetToken(t, sizeof(t)) > 0) {
        if (!strcmp(t, "{")) depth++;
        else if (!strcmp(t, "}")) depth--;
    }
}

template <class ARR, class FN>
static void sReadArray(datTokenizer &tok, ARR &arr, FN read)
{
    int n = tok.GetInt();
    if (n <= 0) return;                 // "Nrm 0" has no block
    tok.MatchToken("{");
    for (int i = 0; i < n; i++) arr.Append(read(tok));
    tok.MatchToken("}");
}

void mshMesh::Serialize(mshSerializer &ser)
{
    datTokenizer &tok = ser.Tok;
    char t[256];
    Reset();
    tok.MatchToken("{");
    while (tok.GetToken(t, sizeof(t)) > 0) {
        if (!strcmp(t, "}")) break;
        if (!_stricmp(t, "Skinned") || !_stricmp(t, "PosSkin") || !_stricmp(t, "Offset")) { tok.GetInt(); }
        else if (!_stricmp(t, "Pos")) sReadArray(tok, m_Positions, [](datTokenizer &k) { Vector3 v; v.x = k.GetFloat(); v.y = k.GetFloat(); v.z = k.GetFloat(); return v; });
        else if (!_stricmp(t, "Nrm")) sReadArray(tok, m_Normals,   [](datTokenizer &k) { Vector3 v; v.x = k.GetFloat(); v.y = k.GetFloat(); v.z = k.GetFloat(); return v; });
        else if (!_stricmp(t, "Cpv")) sReadArray(tok, m_Colors,    [](datTokenizer &k) { Vector4 v; v.x = k.GetFloat(); v.y = k.GetFloat(); v.z = k.GetFloat(); v.w = k.GetFloat(); return v; });
        else if (!_stricmp(t, "Tex0")) sReadArray(tok, m_Tex0,     [](datTokenizer &k) { Vector2 v; v.x = k.GetFloat(); v.y = k.GetFloat(); return v; });
        else if (!_stricmp(t, "Tex1")) sReadArray(tok, m_Tex1,     [](datTokenizer &k) { Vector2 v; v.x = k.GetFloat(); v.y = k.GetFloat(); return v; });
        else if (!_stricmp(t, "Adj")) {
            int n = tok.GetInt();
            if (n > 0) {
                tok.MatchToken("{");
                AdjInfo cur; bool have = false;
                while (tok.GetToken(t, sizeof(t)) > 0) {
                    if (!strcmp(t, "}")) break;
                    if (!_stricmp(t, "P")) { if (have) m_Adjs.Append(cur); cur = AdjInfo(); cur.P = tok.GetInt(); have = true; }
                    else if (!_stricmp(t, "N")) cur.N = tok.GetInt();
                    else if (!_stricmp(t, "C0")) cur.C0 = tok.GetInt();
                    else if (!_stricmp(t, "C1")) tok.GetInt();
                    else if (!_stricmp(t, "T0")) cur.T0 = tok.GetInt();
                    else if (!_stricmp(t, "T1")) cur.T1 = tok.GetInt();
                    else tok.GetInt();          // unknown per-adjunct field
                }
                if (have) m_Adjs.Append(cur);
            }
        }
        else if (!_stricmp(t, "Mtl")) {
            int n = tok.GetInt();
            if (n > 0) {
                tok.MatchToken("{");
                for (int i = 0; i < n; i++) {
                    tok.MatchToken("{");
                    mshMaterial mtl;
                    while (tok.GetToken(t, sizeof(t)) > 0) {
                        if (!strcmp(t, "}")) break;
                        if (!_stricmp(t, "Name")) { tok.GetToken(t, sizeof(t)); mtl.Name = t; }
                        else if (!_stricmp(t, "Priority")) mtl.Priority = tok.GetInt();
                        else if (!_stricmp(t, "Prim")) {
                            int np = tok.GetInt();
                            if (np > 0) {
                                tok.MatchToken("{");
                                for (int p = 0; p < np; p++) {
                                    tok.MatchToken("{");
                                    mshPrimitive prim;
                                    while (tok.GetToken(t, sizeof(t)) > 0) {
                                        if (!strcmp(t, "}")) break;
                                        if (!_stricmp(t, "Type")) {
                                            tok.GetToken(t, sizeof(t));
                                            prim.Type = !_stricmp(t, "TRISTRIP") ? mshTRISTRIP : !_stricmp(t, "TRISTRIP2") ? mshTRISTRIP2 : !_stricmp(t, "QUADS") ? mshQUADS : !_stricmp(t, "POLYGON") ? mshPOLYGON : mshTRIANGLES;
                                        }
                                        else if (!_stricmp(t, "Priority")) prim.Priority = tok.GetInt();
                                        else if (!_stricmp(t, "Idx")) {
                                            int ni = tok.GetInt();
                                            if (ni > 0) { tok.MatchToken("{"); for (int k = 0; k < ni; k++) prim.Idx.Append((mshIndex)tok.GetInt()); tok.MatchToken("}"); }
                                        }
                                        else if (!strcmp(t, "{")) sSkipBlock(tok);
                                    }
                                    mtl.Prim.Append(prim);
                                }
                                tok.MatchToken("}");
                            }
                        }
                        else if (!strcmp(t, "{")) sSkipBlock(tok);
                    }
                    m_Materials.Append(mtl);
                }
                tok.MatchToken("}");
            }
        }
        else if (!strcmp(t, "{")) sSkipBlock(tok);
    }
    // clamp bad indices
    int np = m_Positions.GetCount();
    for (int i = 0; i < m_Adjs.GetCount(); i++) {
        AdjInfo &a = m_Adjs[i];
        if (a.P < 0 || a.P >= np) a.P = 0;
        if (a.N >= m_Normals.GetCount()) a.N = -1;
        if (a.C0 >= m_Colors.GetCount()) a.C0 = -1;
        if (a.T0 >= m_Tex0.GetCount()) a.T0 = -1;
        if (a.T1 >= m_Tex1.GetCount()) a.T1 = -1;
    }
}

bool SerializeFromFile(const char *name, mshMesh &mesh)
{
    char base[256];
    strncpy(base, name, sizeof(base) - 1); base[sizeof(base) - 1] = 0;
    size_t n = strlen(base);
    if (n > 5 && !_stricmp(base + n - 5, ".mesh")) base[n - 5] = 0;
    Stream *s = ASSET.Open(base, "mesh");
    if (!s) {
        char sharedPath[256];
        snprintf(sharedPath, sizeof(sharedPath), "$/vehicle/shared_wheel/%s", base);
        s = ASSET.Open(sharedPath, "mesh");
    }
    if (!s) { Warningf("SerializeFromFile: can't open '%s.mesh'", base); return false; }
    datTokenizer tok;
    tok.Init(base, s);
    mshSerializer ser(tok, false);
    mesh.Serialize(ser);
    s->Close();
    return mesh.m_Positions.GetCount() > 0;
}

// .mod import: go through the regular model loader and unpack its packets.
bool mshMesh::LoadMod(const char *filename)
{
    char base[256];
    strncpy(base, filename, sizeof(base) - 1); base[sizeof(base) - 1] = 0;
    size_t n = strlen(base);
    if (n > 4 && !_stricmp(base + n - 4, ".mod")) base[n - 4] = 0;
    gfxModel *mdl = gfxGetModel(base, 0);
    if (!mdl) return false;
    Reset();
    for (int i = 0; i < mdl->vertices.GetCount(); i++) m_Positions.Append(mdl->vertices[i]);
    for (int i = 0; i < mdl->normals.GetCount(); i++) m_Normals.Append(mdl->normals[i]);
    for (int i = 0; i < mdl->tex_coords.GetCount(); i++) m_Tex0.Append(mdl->tex_coords[i]);
    for (int m = 0; m < mdl->materials.GetCount(); m++) { mshMaterial mtl; mtl.Name = mdl->materials[m].name; m_Materials.Append(mtl); }
    for (int p = 0; p < mdl->packets.GetCount(); p++) {
        const gfxModelPacket &pk = mdl->packets[p];
        int base0 = m_Adjs.GetCount();
        for (int a = 0; a < pk.adjuncts.GetCount(); a++) {
            AdjInfo ai; ai.P = (int)pk.adjuncts[a].vertex_idx; ai.N = (int)pk.adjuncts[a].normal_idx; ai.T0 = pk.adjuncts[a].tex1_idx;
            m_Adjs.Append(ai);
        }
        mshPrimitive prim;
        for (int s = 0; s < pk.strips.GetCount(); s++) {
            const gfxModelStrip &st = pk.strips[s];
            for (int j = 0; j + 2 < st.indices.GetCount(); j++) {
                bool swap = (j & 1) != 0; if (st.type == 2) swap = !swap;
                u32 i0 = st.indices[j], i1 = st.indices[j + 1], i2 = st.indices[j + 2];
                if (swap) { u32 tmp = i0; i0 = i1; i1 = tmp; }
                prim.Idx.Append((mshIndex)(base0 + i0)); prim.Idx.Append((mshIndex)(base0 + i1)); prim.Idx.Append((mshIndex)(base0 + i2));
            }
        }
        int mi = (int)pk.material_index; if (mi < 0 || mi >= m_Materials.GetCount()) mi = 0;
        if (m_Materials.GetCount() == 0) m_Materials.Append(mshMaterial());
        m_Materials[mi].Prim.Append(prim);
    }
    gfxFreeModel(mdl);
    return m_Positions.GetCount() > 0;
}

void mshMesh::Draw() const
{
    for (int m = 0; m < m_Materials.GetCount(); m++) {
        const mshMaterial &mtl = m_Materials[m];
        for (int p = 0; p < mtl.Prim.GetCount(); p++) {
            const mshPrimitive &prim = mtl.Prim[p];
            if (prim.Type != mshTRIANGLES || prim.Idx.GetCount() < 3) continue;
            vglBegin(drawTriangles, (prim.Idx.GetCount() / 3) * 3);
            for (int k = 0; k + 2 < prim.Idx.GetCount(); k += 3) {
                for (int e = 0; e < 3; e++) {
                    int ai = prim.Idx[k + e];
                    if (ai < 0 || ai >= m_Adjs.GetCount()) ai = 0;
                    const AdjInfo &a = m_Adjs[ai];
                    if (a.T0 >= 0) vglTexCoord2f(m_Tex0[a.T0]);
                    vglVertex3f(m_Positions[a.P]);
                }
            }
            vglEnd();
        }
    }
}

/* End of file mesh/mesh.cpp */
