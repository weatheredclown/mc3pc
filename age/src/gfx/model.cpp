#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include "gfx/model.h"
#include "data/timemgr.h"
#include "mesh/mesh.h"
#include "data/assetcfg.h"
#include "gfx/vgl.h"
#include "gfx/rstate.h"
#include "gfx/texture.h"
#include "gfx/simple.h"
#include "gfx/viewport.h"
#include "core/stream.h"
#include "data/token.h"
#include "core/output.h"

#include "atl/array.h"
#include "atl/bitset.h"
#include "data/memory.h"
#include <string>
#include <sstream>
#include <algorithm>
#include <memory>
#include <cstring>

const char *gfxModelFilenameExtension = "mod";


template <class M> static inline float sDet3x3(const M &m) {
    return m.a.x * (m.b.y * m.c.z - m.b.z * m.c.y)
         - m.a.y * (m.b.x * m.c.z - m.b.z * m.c.x)
         + m.a.z * (m.b.x * m.c.y - m.b.y * m.c.x);
}


// Normal handling for bone-driven draws (-nrmmode N, default 1):
//   1 = raw model normal (default) -- the PS2 .mod files store normals in MODEL
//       space even though the verts are bone-local (the exporter's separate
//       "mtxn" table is not used here); rotating them by the vertex bone lit
//       characters as black silhouettes.
//   0 = rotate by the vertex bone 3x3, 2 = transpose rotation, 3 = negated.
#include "data/args.h"
#include "gfx/gputimer.h"   // -gputime scopes
static int sNormalMode() {
    static int mode = -1;
    if (mode < 0) { const char *v = NULL; mode = (args::sm_Instance && ARGS.Get("nrmmode", 0, &v) && v) ? atoi(v) : 1; }
    return mode;
}
// -identitybones: skin every vertex against an identity matrix instead of its
// bone's.  A diagnostic, not a mode: it answers what space the .mod's vertices
// are in.  If they are bone-local the model falls into a heap at the origin,
// each piece sitting at its own bone's local origin; if they are model-space it
// stands up correctly assembled, because then no transform was ever needed.
static bool sIdentityBones() {
    static int on = -1;
    if (on < 0) on = (args::sm_Instance && ARGS.Get("identitybones")) ? 1 : 0;
    return on != 0;
}

template <class M> static inline Vector3 sRotT3x3(const M &m, const Vector3 &n) {
    return Vector3(n.x * m.a.x + n.y * m.a.y + n.z * m.a.z,
                   n.x * m.b.x + n.y * m.b.y + n.z * m.b.z,
                   n.x * m.c.x + n.y * m.c.y + n.z * m.c.z);
}
template <class M> static inline void sNormalModeFixup(Vector3 &n0, Vector3 &n1, Vector3 &n2, const M &m0, const M &m1, const M &m2,
        const atArray<Vector3> &normals, u32 i0, u32 i1, u32 i2) {
    int mode = sNormalMode();
    if (mode == 0) return;
    Vector3 r0(0,1,0), r1(0,1,0), r2(0,1,0);
    if (i0 < (u32)normals.GetCount()) r0 = normals[i0];
    if (i1 < (u32)normals.GetCount()) r1 = normals[i1];
    if (i2 < (u32)normals.GetCount()) r2 = normals[i2];
    if (mode == 1) { n0 = r0; n1 = r1; n2 = r2; }
    else if (mode == 2) { n0 = sRotT3x3(m0, r0); n1 = sRotT3x3(m1, r1); n2 = sRotT3x3(m2, r2); }
    else if (mode == 3) { n0.Negate(); n1.Negate(); n2.Negate(); }
}

// Rotate a model-space normal by the 3x3 part of a Matrix34 (rows a,b,c).
static inline Vector3 sRotate3x3(const Matrix34 &m, const Vector3 &n) {
    return Vector3(n.x * m.a.x + n.y * m.b.x + n.z * m.c.x,
                   n.x * m.a.y + n.y * m.b.y + n.z * m.c.y,
                   n.x * m.a.z + n.y * m.b.z + n.z * m.c.z);
}

// Skin one packet's adjuncts into `outPos` / `outNrm`, blending across every
// matrix that influences them.
//
// A .mod stores a vertex in the local space of the matrix it is bound to.  Where
// several matrices share a vertex the file carries, in addition, one "reskin"
// record per extra matrix: the same vertex written in THAT matrix's local space,
// and the weight it pulls with.  The matrix the adjunct names keeps whatever
// weight the extra ones leave over, so the skinned position is
//
//     (1 - sum wi) * Mprimary * v  +  sum( wi * Mi * vi )
//
// Using only the first term is rigid binding.  For a mesh that is actually
// smooth-skinned that tears it apart at the joints - a fifth of the rider's
// adjuncts carry one or two extra influences pulling up to half the weight, and
// its hands ended up welded to its chest.  Rigid models carry no reskin records
// and come out of here exactly as they did before.
//
// Done once per packet rather than per triangle: an adjunct is shared by several
// triangles, and a blended one is not free.  Templated because the two draw
// paths hand us different matrix types; only the a/b/c/d rows are touched.
template <class M>
static void sSkinPacketAdjuncts(const gfxModelPacket &packet, const M *matrices,
                                const atArray<Vector3> &vertices, const atArray<Vector3> &normals,
                                atArray<Vector3> &outPos, atArray<Vector3> &outNrm) {
    const int nAdj = packet.adjuncts.GetCount();
    const int nBones = packet.bone_map.GetCount();
    outPos.Resize(nAdj);
    outNrm.Resize(nAdj);

    for (int i = 0; i < nAdj; i++) {
        const gfxModelAdjunct &adj = packet.adjuncts[i];
        Vector3 p(0.0f, 0.0f, 0.0f), n(0.0f, 1.0f, 0.0f);
        if (adj.vertex_idx < (u32)vertices.GetCount()) p = vertices[adj.vertex_idx];
        if (adj.normal_idx < (u32)normals.GetCount()) n = normals[adj.normal_idx];

        if (!matrices || sIdentityBones()) { outPos[i] = p; outNrm[i] = n; continue; }

        u32 g = 0;
        if (adj.bone_idx < (u32)nBones) g = packet.bone_map[adj.bone_idx];
        const M &mp = matrices[g];
        Vector3 basePos(p.x * mp.a.x + p.y * mp.b.x + p.z * mp.c.x + mp.d.x,
                        p.x * mp.a.y + p.y * mp.b.y + p.z * mp.c.y + mp.d.y,
                        p.x * mp.a.z + p.y * mp.b.z + p.z * mp.c.z + mp.d.z);
        Vector3 baseNrm(n.x * mp.a.x + n.y * mp.b.x + n.z * mp.c.x,
                        n.x * mp.a.y + n.y * mp.b.y + n.z * mp.c.y,
                        n.x * mp.a.z + n.y * mp.b.z + n.z * mp.c.z);

        Vector3 blendPos(0.0f, 0.0f, 0.0f), blendNrm(0.0f, 0.0f, 0.0f);
        float extra = 0.0f;
        for (int r = 0; r < packet.reskins.GetCount(); r++) {
            const gfxModelReskin &rs = packet.reskins[r];
            if (rs.a != i) continue;
            if (rs.b < 0 || rs.b >= nBones) continue;
            const float w = rs.w[0];
            if (w <= 0.0f) continue;
            const M &m = matrices[packet.bone_map[rs.b]];
            const Vector3 lp(rs.w[1], rs.w[2], rs.w[3]);
            blendPos.AddScaled(Vector3(lp.x * m.a.x + lp.y * m.b.x + lp.z * m.c.x + m.d.x,
                                       lp.x * m.a.y + lp.y * m.b.y + lp.z * m.c.y + m.d.y,
                                       lp.x * m.a.z + lp.y * m.b.z + lp.z * m.c.z + m.d.z), w);
            blendNrm.AddScaled(Vector3(n.x * m.a.x + n.y * m.b.x + n.z * m.c.x,
                                       n.x * m.a.y + n.y * m.b.y + n.z * m.c.y,
                                       n.x * m.a.z + n.y * m.b.z + n.z * m.c.z), w);
            extra += w;
        }

        if (extra <= 0.0f) { outPos[i] = basePos; outNrm[i] = baseNrm; continue; }
        if (extra > 1.0f) {                       // never seen, but do not invert the primary
            const float inv = 1.0f / extra;
            blendPos.Scale(inv); blendNrm.Scale(inv);
            extra = 1.0f;
        }
        const float wPrim = 1.0f - extra;
        Vector3 pos(blendPos); pos.AddScaled(basePos, wPrim);
        Vector3 nrm(blendNrm); nrm.AddScaled(baseNrm, wPrim);
        outPos[i] = pos;
        outNrm[i] = (nrm.Mag2() > 1e-12f) ? (nrm * nrm.InvMag()) : baseNrm;
    }
}

static inline gfxPackedColor sModulateColor(gfxPackedColor vertCol, float mr, float mg, float mb) {
    u32 a = (vertCol >> 24) & 0xff;
    u32 r = (u32)(((vertCol >> 16) & 0xff) * mr);
    u32 g = (u32)(((vertCol >> 8) & 0xff) * mg);
    u32 b = (u32)((vertCol & 0xff) * mb);
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return (a << 24) | (r << 16) | (g << 8) | b;
}

// ============================================================================
// Helpers
// ============================================================================

static std::string trim(const std::string &str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

static atArray<std::string> split_whitespace(const std::string &str) {
    atArray<std::string> parts;
    std::string current;
    for (char c : str) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (!current.empty()) {
                parts.Append(current);
                current.clear();
            }
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) {
        parts.Append(current);
    }
    return parts;
}

class BinaryReader {
    const uint8_t *data;
    size_t len;
    size_t off;
public:
    BinaryReader(const uint8_t *d, size_t l) : data(d), len(l), off(0) {}

    bool Seek(size_t o) {
        if (o > len) return false;
        off = o;
        return true;
    }

    size_t Tell() const { return off; }
    size_t GetLen() const { return len; }

    bool HasBytes(size_t n) const {
        return off + n <= len;
    }

    bool ReadBytes(uint8_t *out, size_t n) {
        if (!HasBytes(n)) return false;
        memcpy(out, data + off, n);
        off += n;
        return true;
    }

    uint32_t ReadU32() {
        if (!HasBytes(4)) return 0;
        uint32_t val = data[off] | (data[off + 1] << 8) | (data[off + 2] << 16) | (data[off + 3] << 24);
        off += 4;
        return val;
    }

    float ReadF32() {
        uint32_t val = ReadU32();
        float f;
        memcpy(&f, &val, 4);
        return f;
    }

    std::string ReadSpaceString() {
        std::string s;
        while (off < len && data[off] != ' ' && data[off] != '\0') {
            s.push_back(data[off]);
            off++;
        }
        if (off < len) off++; // skip delimiter
        return s;
    }

    std::string ReadNullString() {
        std::string s;
        while (off < len && data[off] != '\0') {
            s.push_back(data[off]);
            off++;
        }
        while (off < len && data[off] == '\0') {
            off++;
        }
        return s;
    }

    void Skip(size_t n) {
        if (off + n <= len) off += n;
        else off = len;
    }
};

// ============================================================================
// ASCII/Text .mod parser
// ============================================================================

static bool ParseModAscii(const std::string &content, gfxModel *model) {
    atArray<std::string> lines;
    std::string line;
    std::istringstream stream(content);
    while (std::getline(stream, line)) {
        lines.Append(line);
    }

    int i = 0;
    while (i < lines.GetCount()) {
        std::string trimmed = trim(lines[i]);
        if (trimmed.empty()) {
            i++;
            continue;
        }

        // Vertex
        if (trimmed.rfind("v ", 0) == 0 || trimmed.rfind("v\t", 0) == 0) {
            auto parts = split_whitespace(trimmed);
            if (parts.GetCount() >= 4) {
                float x = std::stof(parts[1]);
                float y = std::stof(parts[2]);
                float z = std::stof(parts[3]);
                model->vertices.Append(Vector3(x, y, z));
            }
        }
        // Normal
        else if (trimmed.rfind("n ", 0) == 0 || trimmed.rfind("n\t", 0) == 0) {
            auto parts = split_whitespace(trimmed);
            if (parts.GetCount() >= 4) {
                float x = std::stof(parts[1]);
                float y = std::stof(parts[2]);
                float z = std::stof(parts[3]);
                model->normals.Append(Vector3(x, y, z));
            }
        }
        // Color
        else if (trimmed.rfind("c ", 0) == 0 || trimmed.rfind("c\t", 0) == 0) {
            auto parts = split_whitespace(trimmed);
            if (parts.GetCount() >= 5) {
                float r = std::stof(parts[1]);
                float g = std::stof(parts[2]);
                float b = std::stof(parts[3]);
                float a = std::stof(parts[4]);
                model->colors.Append(mkfrgba(r, g, b, a));
            }
        }
        else if (trimmed.rfind("t1 ", 0) == 0 || trimmed.rfind("t1\t", 0) == 0) {
            auto parts = split_whitespace(trimmed);
            if (parts.GetCount() >= 3) {
                float u = std::stof(parts[1]);
                float v = std::stof(parts[2]);
                model->tex_coords.Append(Vector2(u, v));
            }
        }
        // Material
        else if (trimmed.rfind("mtl ", 0) == 0 && trimmed.find('{') != std::string::npos) {
            auto parts = split_whitespace(trimmed);
            gfxModelMaterial mat;
            if (parts.GetCount() >= 2) {
                mat.name = parts[1];
            }
            mat.diffuse[0] = 0.8f; mat.diffuse[1] = 0.8f; mat.diffuse[2] = 0.8f;
            mat.primitive_count = 0;
            mat.packet_count = 1;

            i++;
            while (i < lines.GetCount()) {
                std::string mtl_line = trim(lines[i]);
                if (mtl_line == "}") {
                    break;
                }
                if (mtl_line.rfind("diffuse:", 0) == 0) {
                    auto mtl_parts = split_whitespace(mtl_line);
                    if (mtl_parts.GetCount() >= 4) {
                        mat.diffuse[0] = std::stof(mtl_parts[1]);
                        mat.diffuse[1] = std::stof(mtl_parts[2]);
                        mat.diffuse[2] = std::stof(mtl_parts[3]);
                    }
                } else if (mtl_line.rfind("texture:", 0) == 0) {
                    auto mtl_parts = split_whitespace(mtl_line);
                    if (mtl_parts.GetCount() >= 3) {
                        std::string tex = mtl_parts[2];
                        if (tex.size() >= 2 && tex.front() == '"' && tex.back() == '"') {
                            tex = tex.substr(1, tex.size() - 2);
                        }
                        mat.texture_name = tex;
                    }
                } else if (mtl_line.rfind("primitives:", 0) == 0) {
                    auto mtl_parts = split_whitespace(mtl_line);
                    if (mtl_parts.GetCount() >= 2) {
                        mat.primitive_count = std::stoul(mtl_parts[1]);
                    }
                } else if (mtl_line.rfind("packets:", 0) == 0) {
                    auto mtl_parts = split_whitespace(mtl_line);
                    if (mtl_parts.GetCount() >= 2) {
                        mat.packet_count = std::stoul(mtl_parts[1]);
                    }
                }
                i++;
            }
            model->materials.Append(mat);
        }
        // Packet
        else if (trimmed.rfind("packet ", 0) == 0 && trimmed.find('{') != std::string::npos) {
            gfxModelPacket pkt;
            i++;
            while (i < lines.GetCount()) {
                std::string pkt_line = trim(lines[i]);
                if (pkt_line == "}") {
                    break;
                }
                if (pkt_line.rfind("adj", 0) == 0) {
                    auto pkt_parts = split_whitespace(pkt_line);
                    if (pkt_parts.GetCount() >= 5) {
                        uint32_t v = std::stoul(pkt_parts[1]);
                        uint32_t n = std::stoul(pkt_parts[2]);
                        uint32_t c = std::stoul(pkt_parts[3]);
                        int32_t t1 = std::stol(pkt_parts[4]);
                        int32_t t2 = -1;
                        uint32_t bone = 0;
                        if (pkt_parts.GetCount() >= 7) {
                            t2 = std::stol(pkt_parts[5]);
                            bone = std::stoul(pkt_parts[6]);
                        }
                        pkt.adjuncts.Append({(u32)v, (u32)n, (u32)c, (int)t1, (int)t2, (u32)bone});
                    }
                } else if (pkt_line.rfind("stp", 0) == 0 || pkt_line.rfind("str", 0) == 0 || pkt_line.rfind("tri", 0) == 0) {
                    auto pkt_parts = split_whitespace(pkt_line);
                    gfxModelStrip strip;
                    if (pkt_line.rfind("tri", 0) == 0) {
                        if (pkt_parts.GetCount() >= 4) {
                            strip.type = 1;
                            strip.indices.Append(std::stoul(pkt_parts[1]));
                            strip.indices.Append(std::stoul(pkt_parts[2]));
                            strip.indices.Append(std::stoul(pkt_parts[3]));
                            pkt.strips.Append(strip);
                        }
                    } else if (pkt_parts.GetCount() >= 2) {
                        strip.type = (pkt_line.rfind("stp", 0) == 0) ? 2 : 1;
                        uint32_t count = std::stoul(pkt_parts[1]);
                        for (uint32_t idx = 0; idx < count && (2 + idx) < pkt_parts.GetCount(); ++idx) {
                            strip.indices.Append(std::stoul(pkt_parts[2 + idx]));
                        }
                        pkt.strips.Append(strip);
                    }
                } else if (pkt_line.rfind("mtx", 0) == 0 && pkt_line.rfind("mtxv", 0) != 0 && pkt_line.rfind("mtxn", 0) != 0) {
                    auto pkt_parts = split_whitespace(pkt_line);
                    for (int m_idx = 1; m_idx < pkt_parts.GetCount(); ++m_idx) {
                        pkt.bone_map.Append(std::stoul(pkt_parts[m_idx]));
                    }
                }
                i++;
            }

            // Assign material index
            pkt.material_index = 0;
            uint32_t p = (uint32_t)model->packets.GetCount();
            uint32_t pkt_accum = 0;
            for (int m = 0; m < model->materials.GetCount(); ++m) {
                pkt_accum += model->materials[m].packet_count;
                if (p < pkt_accum) {
                    pkt.material_index = (uint32_t)m;
                    break;
                }
            }

            model->packets.Append(pkt);
        } else if (trimmed.rfind("matrices:", 0) == 0) {
            auto parts = split_whitespace(trimmed);
            if (parts.GetCount() >= 2) {
                model->MatrixCount = std::stoi(parts[1]);
            }
        }

        i++;
    }

    return true;
}

// ============================================================================
// Binary v2.10 .mod parser
// ============================================================================

static void sMaterialFromShader(gfxModelMaterial &mat);  // defined with the ascii parser below

static bool ParseModBinary(const uint8_t *data, size_t data_len, gfxModel *model) {
    BinaryReader reader(data, data_len);
    if (!reader.HasBytes(58)) return false;

    uint8_t header[13];
    reader.ReadBytes(header, 13);
    if (memcmp(header, "version: 2.10", 13) != 0) {
        return false;
    }
    reader.Seek(14);

    uint32_t n_verts = reader.ReadU32();
    uint32_t n_normals = reader.ReadU32();
    uint32_t n_colors = reader.ReadU32();
    uint32_t n_tex1s = reader.ReadU32();
    uint32_t n_tex2s = reader.ReadU32();
    uint32_t n_tangents = reader.ReadU32();
    uint32_t n_materials = reader.ReadU32();
    uint32_t n_adjuncts = reader.ReadU32();
    uint32_t n_primitives = reader.ReadU32();
    uint32_t n_matrices = reader.ReadU32();
    uint32_t n_reskins = reader.ReadU32();

    model->MatrixCount = n_matrices;
    model->m_IsModelRelative = false; // TODO: Ideally there'd be a way to figure this out

    reader.Seek(58);

    // Read vertices: n_verts * 3 * float
    for (uint32_t i = 0; i < n_verts; ++i) {
        float x = reader.ReadF32();
        float y = reader.ReadF32();
        float z = reader.ReadF32();
        model->vertices.Append(Vector3(x, y, z));
    }

    // Read normals: n_normals * 3 * float
    for (uint32_t i = 0; i < n_normals; ++i) {
        float x = reader.ReadF32();
        float y = reader.ReadF32();
        float z = reader.ReadF32();
        model->normals.Append(Vector3(x, y, z));
    }

    // Read colors: n_colors * 4 * float
    for (uint32_t i = 0; i < n_colors; ++i) {
        float r = reader.ReadF32();
        float g = reader.ReadF32();
        float b = reader.ReadF32();
        float a = reader.ReadF32();
        model->colors.Append(mkfrgba(r, g, b, a));
    }

    // Read tex1s: n_tex1s * 2 * float
    for (uint32_t i = 0; i < n_tex1s; ++i) {
        float u = reader.ReadF32();
        float v = reader.ReadF32();
        model->tex_coords.Append(Vector2(u, v));
    }

    // Read tex2s (second UV set: lightmaps / detail decals via "texsrc 1")
    for (uint32_t i = 0; i < n_tex2s; ++i) {
        float u = reader.ReadF32();
        float v = reader.ReadF32();
        model->tex_coords2.Append(Vector2(u, v));
    }
    // Skip tangents
    reader.Skip(n_tangents * 12);

    // Parse materials
    for (uint32_t i = 0; i < n_materials; ++i) {
        gfxModelMaterial mat;
        mat.name = reader.ReadSpaceString();

        mat.packet_count = reader.ReadU32();
        mat.primitive_count = reader.ReadU32();
        uint32_t tex_count = reader.ReadU32();
        uint32_t illum = reader.ReadU32();

        // skip ambient (3 floats)
        reader.Skip(12);
        mat.diffuse[0] = reader.ReadF32();
        mat.diffuse[1] = reader.ReadF32();
        mat.diffuse[2] = reader.ReadF32();
        // skip specular (3 floats)
        reader.Skip(12);

        // skip extra values until printable ASCII char (texture name)
        while (reader.HasBytes(4)) {
            size_t curr = reader.Tell();
            uint8_t c = data[curr];
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
                break;
            }
            reader.Skip(4);
        }

        mat.texture_name = reader.ReadNullString();
        // PC PORT: same as the ascii path - overlay <mtl>.shader for state
        // flags (lighting none / depthwrite off) and any texture the .mod
        // itself doesn't name.
        sMaterialFromShader(mat);
        model->materials.Append(mat);
    }

    // Parse packets
    uint32_t total_pkts = 0;
    for (const auto &mat : model->materials) {
        total_pkts += mat.packet_count;
    }

    for (uint32_t p = 0; p < total_pkts; ++p) {
        gfxModelPacket pkt;
        uint32_t adj_count = reader.ReadU32();
        uint32_t strip_count = reader.ReadU32();
        uint32_t mtx_count = reader.ReadU32();
        uint32_t mw_count = reader.ReadU32();

        for (uint32_t a = 0; a < adj_count; ++a) {
            uint32_t v = reader.ReadU32();
            uint32_t n = reader.ReadU32();
            uint32_t c = reader.ReadU32();
            int32_t t1 = (int32_t)reader.ReadU32();
            int32_t t2 = (int32_t)reader.ReadU32();
            uint32_t bone = reader.ReadU32();
            pkt.adjuncts.Append({(u32)v, (u32)n, (u32)c, (int)t1, (int)t2, (u32)bone});
        }

        // skip multiweight/reskin
        reader.Skip(mw_count * 24);

        for (uint32_t s = 0; s < strip_count; ++s) {
            uint32_t stype = reader.ReadU32();
            uint32_t scount = reader.ReadU32();
            gfxModelStrip strip;
            strip.type = stype;
            for (uint32_t i = 0; i < scount; ++i) {
                strip.indices.Append(reader.ReadU32());
            }
            pkt.strips.Append(strip);
        }

        for (uint32_t b = 0; b < mtx_count; ++b) {
            pkt.bone_map.Append(reader.ReadU32());
        }

        // Assign packet's material index
        pkt.material_index = 0;
        uint32_t pkt_accum = 0;
        for (int m = 0; m < model->materials.GetCount(); ++m) {
            pkt_accum += model->materials[m].packet_count;
            if (p < pkt_accum) {
                pkt.material_index = (uint32_t)m;
                break;
            }
        }

        model->packets.Append(pkt);
    }

    return true;
}

// ============================================================================
// v1.10 .mod reader — one structured, fixed-order parse via the tokenizer.
//
// The labels ("verts", "v", "mtl", ...) and the ':' / '{' / '}' delimiters are
// cosmetic in ascii and would be absent / no-op'd by a binary tokenizer, so the
// same call sequence reads either encoding.  The format is fully count-driven:
// header counts, then that many of each record; each material and packet header
// carries its own sub-counts, so nothing is read "until a delimiter".
// ============================================================================

// Read "<label> [:] <int>".  MatchToken enforces the fixed field order in ascii;
// a binary tokenizer no-ops the label.  ':' is cosmetic, hence CheckToken.
static int sHdrInt(datAsciiTokenizer &tok, const char *label) {
    tok.MatchToken(label);
    tok.CheckToken(":");
    return tok.GetInt();
}

static void sHdrVec(datAsciiTokenizer &tok, const char *label, Vector3 &v) {
    tok.MatchToken(label);
    tok.CheckToken(":");
    tok.GetVector(v);
}

// Materials with "textures: 0" in the .mod take their textures from the
// <material>.shader next to it: pass 1 "texture" is stage 0, the "nextpass"
// texture (texsrc 1) is stage 1, blended by its "blendset" (lightmap =
// modulate, otherwise decal over).
static void sMaterialFromShader(gfxModelMaterial &mat) {
    Stream *st = ASSET.Open(mat.name.c_str(), "shader");
    if (!st) return;
    // Materials are often named generically (lambert2SG, ...), and ASSET's
    // search path can hand back ANOTHER entity's shader of the same name.
    // The overlay below therefore VALIDATES before committing flags: it
    // applies only when the material is untextured (the classic textures:0
    // scheme) or when the shader's pass-0 texture matches the texture the
    // .mod itself names - a foreign same-named shader fails that test.
    // (Without this, EndlessCity's platforms picked up projectionUnit's
    // lambert4SG glow flags and vanished into the wrong bucket.)
    const bool hadTexture = !mat.texture_name.empty();
    std::string text; char buf[512]; int n;
    while ((n = st->Read(buf, sizeof(buf))) > 0) text.append(buf, n);
    st->Close();
    gfxModelMaterial parsed;             // staged; committed only if valid
    parsed.texture_name = mat.texture_name;
    parsed.texture_name2 = mat.texture_name2;
    std::string pass0tex;                // shader's own pass-0 texture name
    int pass = 0;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find('\n', pos);
        std::string line = text.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
        pos = (eol == std::string::npos) ? text.size() : eol + 1;
        char cmd[64] = "", arg[128] = "";
        if (sscanf(line.c_str(), " %63s %127s", cmd, arg) < 1) continue;
        if (!stricmp(cmd, "nextpass")) pass++;
        else if (!stricmp(cmd, "texture")) {
            std::string t = arg; size_t dot = t.rfind('.');
            if (dot != std::string::npos && !stricmp(t.c_str() + dot, ".tex")) t.erase(dot);
            if (pass == 0) {
                if (pass0tex.empty()) pass0tex = t;
                if (parsed.texture_name.empty()) parsed.texture_name = t;
            }
            else if (pass == 1 && parsed.texture_name2.empty()) {
                parsed.texture_name2 = t;
                parsed.tex2_mode = 1;   // decal over the base unless "blendset lightmap"
            }
        }
        else if (!stricmp(cmd, "blendset")) {
            if (pass == 1)
                parsed.tex2_mode = stricmp(arg, "lightmap") ? 1 : 0;
            else if (!stricmp(arg, "add"))
                parsed.additive = true;
        }
        // State flags come from PASS 0 (the base draw); a second pass shares
        // the draw as the decal stage.  drawbucket is pass-blind on purpose:
        // the LAST bucket seen governs the combined draw (bcTrashBurntA puts
        // the dirt pass in bucket 2 - the whole pile draws there, after the
        // opaques, as the artist ordered).
        else if (pass == 0 && !stricmp(cmd, "lighting") && !stricmp(arg, "none"))
            parsed.unlit = true;
        else if (pass == 0 && !stricmp(cmd, "depthwrite") && !stricmp(arg, "off"))
            parsed.no_zwrite = true;
        else if (!stricmp(cmd, "drawbucket"))
            parsed.drawbucket = atoi(arg);
        else if (pass == 0 && (!stricmp(cmd, "slides") || !stricmp(cmd, "slidet"))
                 && !stricmp(arg, "waveform")) {
            // "slides|slidet waveform <amp> sawtooth <rate> time <a> <b>"
            // — a sawtooth (wrapping) UV scroll: offset = frac(t*rate)*amp.
            float amp = 0.0f, rate = 0.0f;
            char c2[64], a2[64], wave[64];
            if (sscanf(line.c_str(), " %63s %63s %f %63s %f",
                       c2, a2, &amp, wave, &rate) == 5
                    && !stricmp(wave, "sawtooth")) {
                if (!stricmp(cmd, "slides")) parsed.scroll_u = amp * rate;
                else                         parsed.scroll_v = amp * rate;
            }
        }
    }

    // Validate: an already-textured material accepts the overlay only when
    // the shader's pass-0 texture names the same texture (strip ".tex").
    if (hadTexture) {
        std::string modTex = mat.texture_name;
        size_t dot = modTex.rfind('.');
        if (dot != std::string::npos && !stricmp(modTex.c_str() + dot, ".tex"))
            modTex.erase(dot);
        if (pass0tex.empty() || stricmp(pass0tex.c_str(), modTex.c_str()))
            return;   // foreign same-named shader - ignore it
    }

    if (mat.texture_name.empty())  mat.texture_name  = parsed.texture_name;
    if (mat.texture_name2.empty()) {
        mat.texture_name2 = parsed.texture_name2;
        mat.tex2_mode     = parsed.tex2_mode;
    }
    mat.unlit      = parsed.unlit;
    mat.no_zwrite  = parsed.no_zwrite;
    mat.drawbucket = parsed.drawbucket;
    mat.scroll_u   = parsed.scroll_u;
    mat.scroll_v   = parsed.scroll_v;
    mat.additive   = parsed.additive;
}

static bool ParseModV110(datAsciiTokenizer &tok, gfxModel *model) {
    // --- Header: version, then 11 counts in fixed order. ---
    tok.MatchToken("version");
    tok.CheckToken(":");
    char version[32];
    tok.GetToken(version, sizeof(version));          // "1.10"

    int nVerts     = sHdrInt(tok, "verts");
    int nNormals   = sHdrInt(tok, "normals");
    int nColors    = sHdrInt(tok, "colors");
    int nTex1s     = sHdrInt(tok, "tex1s");
    int nTex2s     = sHdrInt(tok, "tex2s");
    int nTangents  = sHdrInt(tok, "tangents");
    int nMaterials = sHdrInt(tok, "materials");
    model->SourceAdjunctCount = sHdrInt(tok, "adjuncts");        // informational; kept for Save
    model->SourcePrimitiveCount = sHdrInt(tok, "primitives");    // informational; kept for Save
    int nMatrices  = sHdrInt(tok, "matrices");
    model->SourceReskinCount = sHdrInt(tok, "reskins");          // informational; kept for Save

    model->MatrixCount = nMatrices;

    if (nTangents > 0) {
        Errorf("gfxModel v1.10: tangents(%d) records unsupported", nTangents);
        return false;
    }

    // --- Geometry arrays (each count-driven). ---
    for (int i = 0; i < nVerts; i++) {
        tok.MatchToken("v");
        Vector3 v; tok.GetVector(v);
        model->vertices.Append(v);
    }
    for (int i = 0; i < nNormals; i++) {
        tok.MatchToken("n");
        Vector3 n; tok.GetVector(n);
        model->normals.Append(n);
    }
    for (int i = 0; i < nColors; i++) {
        tok.MatchToken("c");
        float r = tok.GetFloat(), g = tok.GetFloat(), b = tok.GetFloat(), a = tok.GetFloat();
        if (a <= 0.0f) a = 1.0f;
        model->colors.Append(mkfrgba(r, g, b, a));
    }
    for (int i = 0; i < nTex1s; i++) {
        tok.MatchToken("t1");
        float u = tok.GetFloat(), v = tok.GetFloat();
        model->tex_coords.Append(Vector2(u, v));
    }
    // Second UV set (lightmap/detail), indexed by the adjunct's 5th field and
    // sampled through the renderer's stage-2 texture (bailing here used to
    // leave every tex2s model -- e.g. EricArm, the cnkcrane crane -- with a
    // NULL gfxModel and an invisible mesh).
    for (int i = 0; i < nTex2s; i++) {
        tok.MatchToken("t2");
        float u = tok.GetFloat(), v = tok.GetFloat();
        model->tex_coords2.Append(Vector2(u, v));
    }

    // --- Materials. ---
    int totalPackets = 0;
    for (int m = 0; m < nMaterials; m++) {
        tok.MatchToken("mtl");
        gfxModelMaterial mat;
        char name[128]; tok.GetToken(name, sizeof(name));
        mat.name = name;
        tok.MatchToken("{");

        mat.packet_count    = sHdrInt(tok, "packets");
        mat.primitive_count = sHdrInt(tok, "primitives");
        int nTex            = sHdrInt(tok, "textures");

        tok.MatchToken("illum"); tok.CheckToken(":");
        char illum[32]; tok.GetToken(illum, sizeof(illum));   // keyword (e.g. "diffuse")
        mat.illum = illum;

        Vector3 ambient, diffuse, specular;
        sHdrVec(tok, "ambient",  ambient);
        sHdrVec(tok, "diffuse",  diffuse);
        sHdrVec(tok, "specular", specular);
        mat.diffuse[0] = diffuse.x; mat.diffuse[1] = diffuse.y; mat.diffuse[2] = diffuse.z;
        mat.ambient[0] = ambient.x; mat.ambient[1] = ambient.y; mat.ambient[2] = ambient.z;
        mat.specular[0] = specular.x; mat.specular[1] = specular.y; mat.specular[2] = specular.z;

        for (int t = 0; t < nTex; t++) {
            tok.MatchToken("texture"); tok.CheckToken(":");
            int slot = tok.GetInt();                  // texture slot index
            char texName[128]; tok.GetToken(texName, sizeof(texName));   // quotes stripped by tokenizer
            if (slot == 1)
                mat.texture_name2 = texName;          // stage 2 (tex2 UVs)
            else
                mat.texture_name = texName;
        }

        // Remember what the file said before anything edits it.
        mat.source_texture_name = mat.texture_name;
        mat.source_texture_name2 = mat.texture_name2;
        mat.has_source_textures = true;

        // PC PORT: consult <mtl>.shader for EVERY material, not just
        // untextured ones - it never clobbers a texture already named in
        // the .mod, but textured materials still need its state flags
        // (projectionUnit's glow material is "textures: 1" with
        // "lighting none" / "depthwrite off" in lambert4SG.shader).
        sMaterialFromShader(mat);

        // Material attributes: "<type> <name>: <value(s)>" lines
        // (model/vp_shared_headlight: "float shininess: 0.000000").  Nothing
        // on PC reads them, so they are parsed and dropped; an unknown type
        // still fails the load rather than desynchronising the tokenizer.
        int nAttr = sHdrInt(tok, "attributes");
        for (int a = 0; a < nAttr; a++) {
            char type[32], name[64];
            tok.GetToken(type, sizeof(type));
            tok.GetToken(name, sizeof(name));
            tok.CheckToken(":");
            int nValues = 0;
            if (!_stricmp(type, "float") || !_stricmp(type, "int"))
                nValues = 1;
            else if (!_stricmp(type, "vector2"))
                nValues = 2;
            else if (!_stricmp(type, "vector3"))
                nValues = 3;
            else if (!_stricmp(type, "vector4"))
                nValues = 4;
            if (!nValues) {
                Errorf("gfxModel v1.10: material attribute type '%s' (%s) unsupported", type, name);
                return false;
            }
            for (int v = 0; v < nValues; v++)
                tok.GetFloat();
        }
        tok.MatchToken("}");

        totalPackets += (int)mat.packet_count;
        model->materials.Append(mat);
    }

    // --- Packets (count = sum of material packet counts). ---
    for (int p = 0; p < totalPackets; p++) {
        tok.MatchToken("packet");
        int nAdj  = tok.GetInt();
        int nStrp = tok.GetInt();
        int nMtx  = tok.GetInt();
        int nMwC  = 0;
        if (!tok.CheckToken("{", false)) {
            nMwC = tok.GetInt();
        }
        tok.MatchToken("{");

        gfxModelPacket pkt;

        for (int a = 0; a < nAdj; a++) {
            tok.MatchToken("adj");
            u32 vi = (u32)tok.GetInt();
            u32 ni = (u32)tok.GetInt();
            u32 ci = (u32)tok.GetInt();
            int t1 = tok.GetInt();
            int t2 = tok.GetInt();                    // index into tex_coords2
            u32 bi = (u32)tok.GetInt();
            gfxModelAdjunct adj = { vi, ni, ci, t1, t2, bi };
            pkt.adjuncts.Append(adj);
        }

        for (int r = 0; r < nMwC; r++) {
            tok.MatchToken("reskin");
            gfxModelReskin rs;
            rs.a = tok.GetInt();
            rs.b = tok.GetInt();
            rs.w[0] = tok.GetFloat();
            rs.w[1] = tok.GetFloat();
            rs.w[2] = tok.GetFloat();
            rs.w[3] = tok.GetFloat();
            pkt.reskins.Append(rs);
        }

        for (int s = 0; s < nStrp; s++) {
            char kind[16]; tok.GetToken(kind, sizeof(kind));   // "str" | "stp" | "tri"
            gfxModelStrip strip;
            strip.type = (kind[0] == 's' && kind[1] == 't' && kind[2] == 'p') ? 2 : 1;
            int cnt = (strcmp(kind, "tri") == 0) ? 3 : tok.GetInt();
            for (int k = 0; k < cnt; k++)
                strip.indices.Append((u32)tok.GetInt());
            pkt.strips.Append(strip);
        }

        tok.MatchToken("mtx");
        for (int mm = 0; mm < nMtx; mm++)
            pkt.bone_map.Append((u32)tok.GetInt());

        tok.MatchToken("}");

        // material index = which material's packet range contains p
        pkt.material_index = 0;
        int accum = 0;
        for (int mi = 0; mi < model->materials.GetCount(); mi++) {
            accum += (int)model->materials[mi].packet_count;
            if (p < accum) { pkt.material_index = (u32)mi; break; }
        }

        model->packets.Append(pkt);
    }

    // --- Trailing reskin matrix-vertex / matrix-normal counts (optional). ---
    // One entry per matrix, not one number: reading a single int here dropped
    // the rest of the table (and a save then wrote it back truncated).
    if (tok.CheckToken("mtxv"))
        for (int i = 0; i < nMatrices; i++) model->MtxVertCounts.Append(tok.GetInt());
    if (tok.CheckToken("mtxn"))
        for (int i = 0; i < nMatrices; i++) model->MtxNormCounts.Append(tok.GetInt());

    return true;
}

bool gfxModel::Save(Stream *s) const
{
    if (!s) return false;

    // Calculate total primitives, adjuncts and reskins across packets
    int totalAdjuncts = 0;
    int totalPrimitives = 0;
    int totalReskins = 0;
    for (int p = 0; p < packets.GetCount(); p++) {
        totalAdjuncts += packets[p].adjuncts.GetCount();
        totalPrimitives += packets[p].strips.GetCount();
        totalReskins += packets[p].reskins.GetCount();
    }
    // The header counts are the file's own when the model came from one: they
    // are not the totals of what follows ("primitives" counts triangles, not
    // strips; "adjuncts" and "reskins" count something the loader never needed),
    // and nothing here can recompute them.
    if (SourcePrimitiveCount >= 0) totalPrimitives = SourcePrimitiveCount;
    if (SourceAdjunctCount >= 0) totalAdjuncts = SourceAdjunctCount;
    if (SourceReskinCount >= 0) totalReskins = SourceReskinCount;

    fprintf(s, "version: 1.10\n");
    fprintf(s, "verts: %d\n", vertices.GetCount());
    fprintf(s, "normals: %d\n", normals.GetCount());
    fprintf(s, "colors: %d\n", colors.GetCount());
    fprintf(s, "tex1s: %d\n", tex_coords.GetCount());
    fprintf(s, "tex2s: %d\n", tex_coords2.GetCount());
    fprintf(s, "tangents: 0\n");
    fprintf(s, "materials: %d\n", materials.GetCount());
    fprintf(s, "adjuncts: %d\n", totalAdjuncts);
    fprintf(s, "primitives: %d\n", totalPrimitives);
    fprintf(s, "matrices: %d\n", MatrixCount);
    fprintf(s, "reskins: %d\n\n", totalReskins);

    // Vertices
    for (int i = 0; i < vertices.GetCount(); i++) {
        const Vector3 &v = vertices[i];
        fprintf(s, "v\t%.6f\t%.6f\t%.6f\n", v.x, v.y, v.z);
    }
    if (vertices.GetCount() > 0) fprintf(s, "\n");

    // Normals
    for (int i = 0; i < normals.GetCount(); i++) {
        const Vector3 &n = normals[i];
        fprintf(s, "n\t%.6f\t%.6f\t%.6f\n", n.x, n.y, n.z);
    }
    if (normals.GetCount() > 0) fprintf(s, "\n");

    // Colors
    for (int i = 0; i < colors.GetCount(); i++) {
        gfxPackedColor c = colors[i];
        float r = ((c >> 16) & 0xff) / 255.0f;
        float g = ((c >> 8) & 0xff) / 255.0f;
        float b = (c & 0xff) / 255.0f;
        u8 aByte = (c >> 24) & 0xff;
        float a = aByte ? (aByte / 255.0f) : 1.0f;
        fprintf(s, "c\t%.6f\t%.6f\t%.6f\t%.6f\n", r, g, b, a);
    }
    if (colors.GetCount() > 0) fprintf(s, "\n");

    // Tex1
    for (int i = 0; i < tex_coords.GetCount(); i++) {
        const Vector2 &t = tex_coords[i];
        fprintf(s, "t1\t%.6f\t%.6f\n", t.x, t.y);
    }
    if (tex_coords.GetCount() > 0) fprintf(s, "\n");

    // Tex2
    for (int i = 0; i < tex_coords2.GetCount(); i++) {
        const Vector2 &t = tex_coords2[i];
        fprintf(s, "t2\t%.6f\t%.6f\n", t.x, t.y);
    }
    if (tex_coords2.GetCount() > 0) fprintf(s, "\n");

    // Recompute exact packet and primitive counts per material so the header
    // strictly matches the packet blocks written below.
    atArray<int> matPackets;
    atArray<int> matPrimitives;
    matPackets.Resize(materials.GetCount());
    matPrimitives.Resize(materials.GetCount());
    for (int m = 0; m < materials.GetCount(); m++) {
        matPackets[m] = 0;
        matPrimitives[m] = 0;
    }
    for (int p = 0; p < packets.GetCount(); p++) {
        int mi = (int)packets[p].material_index;
        if (mi >= 0 && mi < materials.GetCount()) {
            matPackets[mi]++;
            matPrimitives[mi] += packets[p].strips.GetCount();
        } else if (materials.GetCount() > 0) {
            matPackets[0]++;
            matPrimitives[0] += packets[p].strips.GetCount();
        }
    }

    // Materials
    for (int m = 0; m < materials.GetCount(); m++) {
        const gfxModelMaterial &mat = materials[m];
        // Write the names the file carried, not whatever the material is
        // wearing now: the rider mesh gets its skin swapped at load time, and
        // saving that put a texture the .mod cannot resolve into the .mod.
        const std::string &tex0 = mat.has_source_textures ? mat.source_texture_name : mat.texture_name;
        const std::string &tex1 = mat.has_source_textures ? mat.source_texture_name2 : mat.texture_name2;
        int numTex = 0;
        if (!tex0.empty()) numTex++;
        if (!tex1.empty()) numTex++;

        fprintf(s, "mtl %s {\n", mat.name.c_str());
        fprintf(s, "\tpackets:\t%u\n", (u32)matPackets[m]);
        fprintf(s, "\tprimitives:\t%u\n", (u32)matPrimitives[m]);
        fprintf(s, "\ttextures:\t%d\n", numTex);
        fprintf(s, "\tillum: %s\n", mat.illum.c_str());
        fprintf(s, "\tambient:\t%.6f %.6f %.6f\n", mat.ambient[0], mat.ambient[1], mat.ambient[2]);
        fprintf(s, "\tdiffuse:\t%.6f %.6f %.6f\n", mat.diffuse[0], mat.diffuse[1], mat.diffuse[2]);
        fprintf(s, "\tspecular:\t%.6f %.6f %.6f\n", mat.specular[0], mat.specular[1], mat.specular[2]);
        if (!tex0.empty()) {
            fprintf(s, "\ttexture: 0 \"%s\"\n", tex0.c_str());
        }
        if (!tex1.empty()) {
            fprintf(s, "\ttexture: 1 \"%s\"\n", tex1.c_str());
        }
        fprintf(s, "\tattributes:\t0\n");
        fprintf(s, "}\n\n");
    }

    // Packets (written grouped by material so ParseModV110 maps them 1:1)
    for (int m = 0; m < materials.GetCount(); m++) {
        for (int p = 0; p < packets.GetCount(); p++) {
            int mi = (int)packets[p].material_index;
            if (mi < 0 || mi >= materials.GetCount()) mi = 0;
            if (mi != m) continue;

            const gfxModelPacket &pkt = packets[p];
            int nAdj = pkt.adjuncts.GetCount();
            int nStrp = pkt.strips.GetCount();
            int nMtx = pkt.bone_map.GetCount();
            int nRsk = pkt.reskins.GetCount();

            // The fourth count is optional: the reader takes it only when the token
            // after the third is not "{", so a packet with no reskins keeps the
            // three-count spelling the original files use.
            if (nRsk > 0)
                fprintf(s, "packet %d %d %d %d {\n", nAdj, nStrp, nMtx, nRsk);
            else
                fprintf(s, "packet %d %d %d {\n", nAdj, nStrp, nMtx);
            for (int a = 0; a < nAdj; a++) {
                const gfxModelAdjunct &adj = pkt.adjuncts[a];
                fprintf(s, "\tadj %5u %5u %5u %5d %5d %5u\n",
                        adj.vertex_idx, adj.normal_idx, adj.color_idx,
                        adj.tex1_idx, adj.tex2_idx, adj.bone_idx);
            }
            for (int r = 0; r < nRsk; r++) {
                const gfxModelReskin &rs = pkt.reskins[r];
                fprintf(s, "\treskin %5d %5d %.6f %.6f %.6f %.6f\n",
                        rs.a, rs.b, rs.w[0], rs.w[1], rs.w[2], rs.w[3]);
            }
            for (int st = 0; st < nStrp; st++) {
                const gfxModelStrip &strip = pkt.strips[st];
                const char *kind = (strip.type == 2) ? "stp" : "str";
                fprintf(s, "\t%s %5d", kind, strip.indices.GetCount());
                for (int k = 0; k < strip.indices.GetCount(); k++) {
                    fprintf(s, " %5u", strip.indices[k]);
                }
                fprintf(s, "\n");
            }
            fprintf(s, "\tmtx");
            for (int mm = 0; mm < nMtx; mm++) {
                fprintf(s, " %u", pkt.bone_map[mm]);
            }
            fprintf(s, "\n}\n\n");
        }
    }

    if (MtxVertCounts.GetCount() > 0) {
        fprintf(s, "mtxv");
        for (int i = 0; i < MtxVertCounts.GetCount(); i++) fprintf(s, " %d", MtxVertCounts[i]);
        fprintf(s, "\n");
    }
    if (MtxNormCounts.GetCount() > 0) {
        fprintf(s, "mtxn");
        for (int i = 0; i < MtxNormCounts.GetCount(); i++) fprintf(s, " %d", MtxNormCounts[i]);
        fprintf(s, "\n");
    }

    return true;
}

bool gfxModel::Save(const char *filename) const
{
    if (!filename || !*filename) return false;

    Stream *s = ASSET.Create(filename, "mod");
    if (!s) {
        Errorf("gfxModel::Save: failed to open '%s' for writing", filename);
        return false;
    }

    bool ok = Save(s);
    s->Close();
    if (ok)
        Displayf("gfxModel::Save: wrote '%s' (%d verts, %d packets)", filename, vertices.GetCount(), packets.GetCount());
    return ok;
}

// ============================================================================
// Loader Common Entry Point
// ============================================================================

int gfxGeometry::sm_PositionBits = 16;
int gfxGeometry::sm_TexCoordBits = 16;
int gfxGeometry::sm_NormalBits = 8;

static std::string sAlphaMaterialSuffix;
void gfxModelBase::SetAlphaMaterialSuffix(const char *suffix) { sAlphaMaterialSuffix = suffix ? suffix : ""; }
const char *gfxModelBase::GetAlphaMaterialSuffix() { return sAlphaMaterialSuffix.c_str(); }

// Flag the packets whose material carries the alpha suffix and build the
// draw order that trails them behind the rest of the model.
void gfxModel::BuildDrawOrder() {
    const std::string &suf = sAlphaMaterialSuffix;
    int nLast = 0;
    for (int i = 0; i < packets.GetCount(); i++) {
        gfxModelPacket &pk = packets[i];
        pk.draw_last = false;
        if (pk.material_index < (u32)materials.GetCount()) {
            if (!suf.empty()) {
                const std::string &mname = materials[pk.material_index].name;
                if (mname.size() >= suf.size() && _stricmp(mname.c_str() + mname.size() - suf.size(), suf.c_str()) == 0)
                    pk.draw_last = true;
            }
            if (materials[pk.material_index].city_blend == 3)
                pk.draw_last = true;
        }
        if (pk.draw_last) nLast++;
    }
    m_DrawOrder.Reset();
    if (!nLast) return;
    for (int i = 0; i < packets.GetCount(); i++) if (!packets[i].draw_last) m_DrawOrder.Append(i);
    for (int i = 0; i < packets.GetCount(); i++) if (packets[i].draw_last) m_DrawOrder.Append(i);
}

static gfxModel* LoadModel(const char *name) {
    // Geometry allocations tag the bucket the game selected (gfxMemory).
    datUseMemoryBucket bucketScope(gfxMemory::GetGeometryBucket());
    Stream* pStream = ASSET.Open(name, "mod");
    if (!pStream) {
        pStream = ASSET.Open(name, "xmod");
    }
    if (!pStream) {
        return NULL;
    }

    atArray<uint8_t> data;
    uint8_t chunk[4096];
    while (true) {
        int read = pStream->Read(chunk, sizeof(chunk));
        if (read <= 0) break;
        for (auto it = chunk; it != chunk + read; ++it) data.Append(*it);
    }
    pStream->Close();

    if (data.GetCount() < 14) return NULL;

    gfxModel* model = new gfxModel();

    // Check version
    bool is_binary = false;
    if (data.GetCount() >= 13 && memcmp(data.data(), "version: 2.10", 13) == 0) {
        is_binary = true;
    }

    bool success = false;
    if (is_binary) {
        success = ParseModBinary(data.data(), data.GetCount(), model);
    } else {
        // v1.10 ascii: single structured, fixed-order read via the tokenizer.
        Stream* ts = ASSET.Open(name, "mod");
        if (!ts) ts = ASSET.Open(name, "xmod");
        if (ts) {
            datTokenizer tok;
            tok.Init(name, ts);
            success = ParseModV110(tok, model);
            ts->Close();
        }
    }

    if (!success) {
        delete model;
        return NULL;
    }
    model->BuildDrawOrder();

    if (model->materials.GetCount() > 0) {
        const auto &mat = model->materials[0];
        gfxMaterial gfxMat;
        gfxMat.diffuse = Vector4(mat.diffuse[0], mat.diffuse[1], mat.diffuse[2], 1.0f);
        gfxMat.emissive = Vector4(0.0f, 0.0f, 0.0f, 1.0f);
        model->m_Shader.SetMaterial(&gfxMat);
        
        // Pre-load and cache all material textures while the folder context is pushed.
        for (int m = 0; m < model->materials.GetCount(); ++m) {
            const auto &m_mat = model->materials[m];
            if (!m_mat.texture_name.empty()) {
                gfxTexture* tex = gfxGetTexture(m_mat.texture_name.c_str(), true, false);
                if (m == 0) {
                    model->m_Shader.SetTexture(tex);
                }
            }
            if (!m_mat.texture_name2.empty())
                gfxGetTexture(m_mat.texture_name2.c_str(), true, false);
        }
    } else {
        gfxMaterial gfxMat;
        gfxMat.diffuse = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
        gfxMat.emissive = Vector4(0.0f, 0.0f, 0.0f, 1.0f);
        model->m_Shader.SetMaterial(&gfxMat);
    }

    return model;
}

// ============================================================================
// Public API Implementations
// ============================================================================

// PC PORT: name-keyed model cache (mirrors gfxGetTexture, including negative
// caching of failed loads).  Before this, EVERY gfxGetModel call re-read and
// re-parsed the .mod - harmless while entity types lived forever, but the
// layout-eviction pass re-creates types per layout, which exploded into
// ~50 MB/layout of duplicate geometry.  The cache owns its models: nothing
// may delete a cache-owned gfxModel (see rmcModelGeometry::~rmcModelGeometry).
#include <map>
#include <set>
#include <string>

static std::map<std::string, gfxModel*> s_ModelCache;   // key: lowercased name
static std::set<const gfxModel*> s_ModelCacheOwned;     // fast ownership test

static std::string sModelCacheKey(const char *name) {
    std::string key(name ? name : "");
    for (size_t i = 0; i < key.size(); i++)
        key[i] = (char)tolower((unsigned char)key[i]);
    return key;
}

bool gfxModelIsCacheOwned(const gfxModel *model) {
    return s_ModelCacheOwned.find(model) != s_ModelCacheOwned.end();
}

static gfxModel *GetModelCached(const char *name) {
    if (!name || !name[0])
        return NULL;
    std::string key = sModelCacheKey(name);
    std::map<std::string, gfxModel*>::iterator it = s_ModelCache.find(key);
    if (it != s_ModelCache.end()) {
        if (it->second) it->second->AddRef();   // one reference per hand-out
        return it->second;          // hit (NULL = cached failure)
    }
    gfxModel *model = LoadModel(name);
    s_ModelCache[key] = model;
    if (model) {
        s_ModelCacheOwned.insert(model);
        model->AddRef();
    }
    return model;
}

void gfxModel::Release() {
    if (--RefCount > 0) return;
    RefCount = 0;
    if (gfxModelIsCacheOwned(this)) return;   // freed by gfxModelPruneHashtable
    delete this;
}

// Delete the cached models nobody references (the game calls this between
// level layers so a freed car/prop set actually leaves memory).
void gfxModelPruneHashtable() {
    int pruned = 0;
    for (std::map<std::string, gfxModel*>::iterator it = s_ModelCache.begin(); it != s_ModelCache.end();) {
        gfxModel *m = it->second;
        if (m && m->GetRefCount() <= 0) {
            s_ModelCacheOwned.erase(m);
            delete m;
            it = s_ModelCache.erase(it);
            pruned++;
        } else {
            ++it;
        }
    }
    if (pruned) Displayf("gfxModelPruneHashtable: freed %d model(s), %d cached", pruned, (int)s_ModelCache.size());
}

// Drop every cached model (shutdown / full reload); referenced or not.
void gfxModelKillHashtable() {
    for (std::map<std::string, gfxModel*>::iterator it = s_ModelCache.begin(); it != s_ModelCache.end(); ++it)
        if (it->second) delete it->second;
    s_ModelCache.clear();
    s_ModelCacheOwned.clear();
}

void gfxModelPrintHashtable() {
    Displayf("gfxModel cache: %d entries", (int)s_ModelCache.size());
    for (std::map<std::string, gfxModel*>::const_iterator it = s_ModelCache.begin(); it != s_ModelCache.end(); ++it) {
        const gfxModel *m = it->second;
        if (m) Displayf("  %-40s refs=%d verts=%d packets=%d", it->first.c_str(), m->GetRefCount(), m->vertices.GetCount(), m->packets.GetCount());
        else Displayf("  %-40s (missing)", it->first.c_str());
    }
}

void gfxModel::DeleteModelHash(const char *name) {
    if (!name || !name[0])
        return;
    std::string key = sModelCacheKey(name);
    std::map<std::string, gfxModel*>::iterator it = s_ModelCache.find(key);
    if (it == s_ModelCache.end())
        return;
    gfxModel *model = it->second;
    s_ModelCache.erase(it);
    if (model) {
        s_ModelCacheOwned.erase(model);
        delete model;
    }
}

gfxModel *gfxGetModel(const char *name, int lod) {
    return GetModelCached(name);
}

gfxModel *gfxGetModel(const char *name, int flags, int fvf) {
    return GetModelCached(name);
}

// AGE 2.72 loads <subfolder>/<name>.  Most game callers push the folder
// themselves (vehicle types), so the bare name keeps priority; callers that only
// name it here (the "model" folder: vp_shared_headlight, hud icons, rocket) used
// to get NULL because the subfolder was dropped.
gfxModel *gfxGetModelPrefix(const char *subfolder, const char *name, int flags, int fvf) {
    gfxModel *model = GetModelCached(name);
    if (!model && subfolder && subfolder[0] && name && name[0]) {
        std::string path = std::string(subfolder) + "/" + name;
        model = GetModelCached(path.c_str());
    }
    return model;
}


// Diagnostics: -hidemodel <substr> skips models whose name contains it (case-
// insensitive); -modeldiag prints render state once per model name.
#include "data/args.h"
#include <set>
#include <string>

gfxModel *gfxModelFromMesh(const mshMesh &mesh)
{
    gfxModel *model = new gfxModel();
    for (int i = 0; i < mesh.m_Positions.GetCount(); i++) model->vertices.Append(mesh.m_Positions[i]);
    for (int i = 0; i < mesh.m_Normals.GetCount(); i++) model->normals.Append(mesh.m_Normals[i]);
    for (int i = 0; i < mesh.m_Colors.GetCount(); i++) { const Vector4 &c = mesh.m_Colors[i]; model->colors.Append(mkfrgba(c.x, c.y, c.z, c.w)); }
    for (int i = 0; i < mesh.m_Tex0.GetCount(); i++) model->tex_coords.Append(mesh.m_Tex0[i]);
    for (int i = 0; i < mesh.m_Tex1.GetCount(); i++) model->tex_coords2.Append(mesh.m_Tex1[i]);
    bool hasN = model->normals.GetCount() > 0, hasC = model->colors.GetCount() > 0;
    if (!hasN) model->normals.Append(Vector3(0.0f, 1.0f, 0.0f));
    // 0x80 is the neutral vertex colour under the GS modulate rule, not white.
    if (!hasC) model->colors.Append((gfxPackedColor)0x80808080u);
    if (model->tex_coords.GetCount() == 0) model->tex_coords.Append(Vector2(0.0f, 0.0f));
    model->BuildDrawOrder();

    for (int m = 0; m < mesh.GetMtlCount(); m++) {
        const mshMaterial &src = mesh.GetMtl(m);
        gfxModelMaterial mat;
        mat.name = src.Name;
        mat.diffuse[0] = mat.diffuse[1] = mat.diffuse[2] = 1.0f;
        mat.packet_count = 1; mat.primitive_count = 0;
        sMaterialFromShader(mat);        // "<Name>.shader" supplies the textures
        if (!mat.texture_name.empty()) gfxGetTexture(mat.texture_name.c_str(), true, false);
        if (!mat.texture_name2.empty()) gfxGetTexture(mat.texture_name2.c_str(), true, false);
        model->materials.Append(mat);

        gfxModelPacket pk;
        pk.material_index = (u32)m;
        for (int a = 0; a < mesh.m_Adjs.GetCount(); a++) {
            const mshMesh::AdjInfo &ai = mesh.m_Adjs[a];
            gfxModelAdjunct adj;
            adj.vertex_idx = (u32)ai.P;
            adj.normal_idx = (hasN && ai.N >= 0) ? (u32)ai.N : 0;
            adj.color_idx  = (hasC && ai.C0 >= 0) ? (u32)ai.C0 : 0xFFFFFFFFu;
            adj.tex1_idx   = ai.T0 >= 0 ? ai.T0 : 0;
            adj.tex2_idx   = ai.T1;
            adj.bone_idx   = 0;
            pk.adjuncts.Append(adj);
        }
        int nAdj = pk.adjuncts.GetCount();
        for (int p = 0; p < src.Prim.GetCount(); p++) {
            const mshPrimitive &prim = src.Prim[p];
            const int n = prim.Idx.GetCount();
            auto tri = [&](int a, int b, int c) {
                if (a >= nAdj || b >= nAdj || c >= nAdj) return;
                gfxModelStrip st; st.type = 1;
                st.indices.Append((u32)a); st.indices.Append((u32)b); st.indices.Append((u32)c);
                pk.strips.Append(st);
                model->materials[m].primitive_count++;
            };
            if (prim.Type == mshTRIANGLES) { for (int k = 0; k + 2 < n; k += 3) tri(prim.Idx[k], prim.Idx[k + 1], prim.Idx[k + 2]); }
            // TRISTRIP2 is a strip whose first triangle winds the other way - the "stp"
            // strip the draw walks with its parity flipped.  Read as a triangle list it
            // lost every triangle past the first of each three indices (the holes in the
            // ambient vehicles' hoods and roofs: 30% of their primitives are TRISTRIP2).
            else if (prim.Type == mshTRISTRIP || prim.Type == mshTRISTRIP2) { if (n >= 3) { gfxModelStrip st; st.type = (prim.Type == mshTRISTRIP2) ? 2 : 1; for (int k = 0; k < n; k++) st.indices.Append((u32)prim.Idx[k]); pk.strips.Append(st); } }
            else if (prim.Type == mshQUADS) { for (int k = 0; k + 3 < n; k += 4) { tri(prim.Idx[k], prim.Idx[k + 1], prim.Idx[k + 2]); tri(prim.Idx[k], prim.Idx[k + 2], prim.Idx[k + 3]); } }
            else { for (int k = 1; k + 1 < n; k++) tri(prim.Idx[0], prim.Idx[k], prim.Idx[k + 1]); }
        }
        model->packets.Append(pk);
    }
    if (model->materials.GetCount() > 0) {
        gfxMaterial gfxMat;
        gfxMat.diffuse = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
        gfxMat.emissive = Vector4(0.0f, 0.0f, 0.0f, 1.0f);
        model->m_Shader.SetMaterial(&gfxMat);
        if (!model->materials[0].texture_name.empty())
            model->m_Shader.SetTexture(gfxGetTexture(model->materials[0].texture_name.c_str(), true, false));
    }
    return model;
}

static bool sModelDiagSkip(const gfxModel *m, const char *site) {
  static const char *hide = NULL; static bool diag = false; static bool init = false;
  if (!init) { init = true; if (!ARGS.Get("hidemodel", 0, &hide)) hide = NULL; diag = ARGS.Get("modeldiag"); }
  std::string lname;
  for (int i = 0; i < m->materials.GetCount(); i++) { lname += m->materials[i].texture_name; lname += ","; lname += m->materials[i].texture_name2; lname += ";"; }
  for (size_t i = 0; i < lname.size(); i++) lname[i] = (char)tolower(lname[i]);
  if (diag) {
    static std::set<std::string> seen;
    std::string key = lname + "|" + site;
    if (seen.insert(key).second)
      Displayf("[modeldiag] site=%s verts=%d nrm=%d mats='%s' lightEnable=%d mode=%d", site, (int)m->vertices.GetCount(), (int)m->normals.GetCount(),
               lname.c_str(), (int)RSTATE.GetLighting(), RSTATE.GetLightingMode());
  }
  return hide && strstr(lname.c_str(), hide) != NULL;
}
void gfxModel::Draw(Matrix44 *matrices, int pass) {
    Draw(matrices, pass, -1);
}
void gfxModel::Draw(Matrix44 *matrices, int pass, int cpvIndex) {
    Draw(matrices, pass, (const atBitSet*)nullptr, cpvIndex);
}

// ---------------------------------------------------------------------------
// PC PORT: vehicle material shading (see gfxModelMaterial::car_shade).  The
// pack's shader templates carry no PC shader code, so the look rscview settled
// on is computed per vertex here: hemisphere ambient + key light diffuse +
// Blinn-Phong specular + fresnel clearcoat / environment tint, times the baked
// vertex colour as occlusion.  The key light is the render state's light 0
// (the city's sun / moon) when it is directional, else a fixed key.
static inline float sClamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
static inline float sCarAo(gfxPackedColor c) {
    float l = (((c >> 16) & 0xff) * 0.3f + ((c >> 8) & 0xff) * 0.59f + (c & 0xff) * 0.11f) / 128.0f;
    return 0.55f + 0.45f * sClamp01(l);
}

static inline void sCarEnvColor(const Vector3 &r, float &er, float &eg, float &eb) {
    float y = r.y;
    if (y < 0.0f) {
        float t = sClamp01(-y);
        er = 0.55f + (0.16f - 0.55f) * t; eg = 0.58f + (0.16f - 0.58f) * t; eb = 0.62f + (0.18f - 0.62f) * t;
    } else {
        float t = sClamp01(y);
        er = 0.55f + (0.38f - 0.55f) * t; eg = 0.58f + (0.50f - 0.58f) * t; eb = 0.62f + (0.70f - 0.62f) * t;
    }
}
struct sCarShadeEnv {
    Vector3 camPos;
    Vector3 lightDir;     // towards the key light
    float keyScale;       // key light strength (0..1+)
    float ambScale;       // ambient / environment strength
    // How lit the current material's lens is, 0..1 (see gfxModelMaterial::car_light).
    // Set per packet from the car's live light levels; 0 for everything else.
    float carLight = 0.0f;
    // The drawing model's paint ramp for car_paint materials (null: none pushed yet).
    const gfxCarPaintRamp *paintRamp = nullptr;
    // PC PORT: the lights the car's own light group put in the render state (the city
    // lights nearest the car plus its headlight; mcCity::SetupPlayerCarLighting).  The
    // console drew one specular highlight per light into the paint's specular map
    // (mcCarMetallicPaint::RenderHighlights / DrawHilight), which is why metallic paint
    // catches street lights at night; here each one adds its own highlight instead.
    // Following that code: the nearest city lights (GetClosestLights) each draw a WHITE
    // highlight whose intensity falls off to nothing by 25 m, and the single directional
    // highlight is the coloured one (the headlight at midnight, else the global light).
    struct SpecLight {
        Vector3 dirOrPos;   // towards the light (directional) or its position (point)
        Vector3 color;
        bool isPoint;
    };
    SpecLight specLights[3];
    int numSpecLights = 0;
    // PC PORT: the view basis (gfxRenderState::SetCamera's D3D convention: x =
    // camera a, y = b, z = -c) and whether the live environment probe has data,
    // for sCarEnvFromProbe.
    Vector3 viewX, viewY, viewZ;
    bool envProbe = false;
    // PC PORT: this packet's reflection lands per PIXEL, from the live city
    // environment map bound to stage 2 (tex2colAddByVertexAlpha).  The shading
    // then leaves the reflection's weight in the vertex alpha instead of mixing
    // a per-vertex environment colour into the rgb itself.
    bool envPerPixel = false;
};

// PC PORT: the same reflection colour, but taken from the live city environment
// map (mceffects/envmap.cpp) instead of the fixed hemisphere above.  `r` is the
// world-space reflection vector; the camera basis turns it into view space and
// the sphere-map formula is the one the vertex shader uses for
// texsrcCameraSpaceReflectionVector, so a chrome pass sampling the map on the
// GPU and this per-vertex tint agree about which way is which.
//
// The map is dim next to the analytic gradient it replaces (a night street is
// mostly black), so it is scaled to the gradient's own mid-grey and floored:
// the point is to pick up the CITY's colour - sodium orange, neon, a lit shop
// front - not to relight the car.  -nocarenvmap goes back to the gradient.
static bool sCarEnvFromProbe(const sCarShadeEnv &e, const Vector3 &r, float &er, float &eg, float &eb) {
    if (!e.envProbe) return false;
    const float rx = r.Dot(e.viewX), ry = r.Dot(e.viewY), rz = r.Dot(e.viewZ);
    const float m = 2.0f * sqrtf(rx * rx + ry * ry + (rz - 1.0f) * (rz - 1.0f));
    const float inv = 1.0f / (m > 1e-4f ? m : 1e-4f);
    float u = rx * inv + 0.5f;
    float v = 1.0f - (ry * inv + 0.5f);
    // -carenvflipu / -carenvflipv: the map is rendered with a flipped projection
    // (mcCityEnvMap::Initialize), so which corner of it is "up" is a question the
    // source does not answer.  Settled by looking at a car roof - it must reflect
    // the sky, not the road.
    static const bool sFlipU = ARGS.Get("carenvflipu") != NULL;
    static const bool sFlipV = ARGS.Get("carenvflipv") != NULL;
    if (sFlipU) u = 1.0f - u;
    if (sFlipV) v = 1.0f - v;
    float rgb[3];
    if (!gfxEnvProbeSample(u, v, rgb)) return false;
    // Match the gradient's overall level (~0.58 mid-grey) so the existing
    // coat / reflect weights keep their meaning, and keep a floor so a black
    // sky does not kill the reflection outright.
    static const float kGain = 2.2f, kFloor = 0.10f;
    er = kFloor + rgb[0] * kGain;
    eg = kFloor + rgb[1] * kGain;
    eb = kFloor + rgb[2] * kGain;
    if (er > 1.0f) er = 1.0f;
    if (eg > 1.0f) eg = 1.0f;
    if (eb > 1.0f) eb = 1.0f;
    return true;
}

// Defined in gfx/rgl.cpp: starts the -shotrace capture clock (see below).
extern "C" void gfxNoteCarShadedDraw();


static void sCarShadeEnvSetup(sCarShadeEnv &e) {
    e.camPos = RSTATE.GetCameraPosition();
    {
        const Matrix34 &cam = RSTATE.GetCamera();
        e.viewX = cam.a;
        e.viewY = cam.b;
        e.viewZ = -cam.c;
        static const bool sNoCarEnvMap = ARGS.Get("nocarenvmap") != NULL;
        e.envProbe = !sNoCarEnvMap && gfxEnvProbeValid();
    }
    Vector3 dir, col(0.0f, 0.0f, 0.0f), amb;
    RSTATE.GetAmbientColor(amb);
    float ambLum = amb.x * 0.3f + amb.y * 0.59f + amb.z * 0.11f;
    float keyLum = 0.0f;
    bool haveKey = false;
    for (int li = 0; li < 3; li++) {          // the brightest directional slot is the key
        Vector3 d2, c2;
        if (!RSTATE.GetDirectionalLight(li, d2, c2)) continue;
        float l2 = c2.x * 0.3f + c2.y * 0.59f + c2.z * 0.11f;
        if (!haveKey || l2 > keyLum) { haveKey = true; keyLum = l2; dir = d2; col = c2; }
    }
    if (keyLum > 1.5f) keyLum = 1.5f;
    if (haveKey && keyLum > 0.05f) {
        e.lightDir = dir;
    } else {
        e.lightDir.Set(0.3f, 0.9f, 0.3f);       // night: a soft key stands in for the street lighting
        e.lightDir.Normalize();
    }
    // The render state carries the city's light colours at half scale (0x80 = 1.0
    // convention), so the key is read back up; floors keep night cars readable.
    e.keyScale = keyLum * 1.6f;
    if (e.keyScale < 0.4f) e.keyScale = 0.4f;
    if (e.keyScale > 1.2f) e.keyScale = 1.2f;
    e.ambScale = 0.30f + 1.2f * ambLum + 0.35f * sClamp01(keyLum);
    if (e.ambScale > 1.3f) e.ambScale = 1.3f;
    if (e.lightDir.y < 0.15f) {          // a key below the horizon lights nothing: lift it
        e.lightDir.y = 0.15f;
        e.lightDir.Normalize();
    }
    // Every enabled slot contributes a highlight (the key light is one of them).
    e.numSpecLights = 0;
    for (int li = 0; li < 3; li++) {
        int mode; Vector3 v, c;
        RSTATE.GetLightSlot(li, mode, v, c);
        if (mode == 0) continue;
        if (c.x + c.y + c.z <= 0.001f) continue;
        sCarShadeEnv::SpecLight &sl = e.specLights[e.numSpecLights];
        sl.isPoint = (mode == 2);
        if (sl.isPoint) {
            sl.dirOrPos = v;
            c.Set(1.0f, 1.0f, 1.0f);    // DrawHilight's lColor for a city light
        } else {
            Vector3 towards(-v.x, -v.y, -v.z);
            if (towards.MagSq() < 1e-8f) continue;
            towards.Normalize();
            sl.dirOrPos = towards;
        }
        sl.color = c;
        e.numSpecLights++;
    }
    // -nospeclight: fall back to the single key-light highlight, for A/B shots
    if (ARGS.Get("nospeclight")) e.numSpecLights = 0;

    static int sLogged = 0;
    // -texlog: the first few setups happen while the race is still loading, so sample
    // one again every 600 to catch the live race lighting.
    const int seen = sLogged++;
    if ((seen < 3 || (seen % 600) == 0) && ARGS.Get("texlog")) {
        Displayf("car shading: key light dir (%.2f %.2f %.2f) colour (%.2f %.2f %.2f) ambient colour (%.2f %.2f %.2f) -> key %.2f ambient %.2f", e.lightDir.x, e.lightDir.y, e.lightDir.z, col.x, col.y, col.z, amb.x, amb.y, amb.z, e.keyScale, e.ambScale);
        for (int li = 0; li < 3; li++) {
            int mode; Vector3 v, c;
            RSTATE.GetLightSlot(li, mode, v, c);
            Displayf("  light slot %d: mode %d dir/pos (%.2f %.2f %.2f) colour (%.2f %.2f %.2f)", li, mode, v.x, v.y, v.z, c.x, c.y, c.z);
        }
    }
}

// The paint ramp at u (0..1), as mcCarMetallicPaint::MakeGradient filled the palette:
// linear between the stops, which sit at 0..255.
static void sCarPaintRampAt(const gfxCarPaintRamp &ramp, float u, float &r, float &g, float &b) {
    const float x = sClamp01(u) * 255.0f;
    int i = 0;
    while (i < ramp.count - 2 && x > (float)ramp.pos[i + 1]) i++;
    const float x0 = (float)ramp.pos[i], x1 = (float)ramp.pos[i + 1];
    const float t = (x1 > x0) ? sClamp01((x - x0) / (x1 - x0)) : 1.0f;
    r = ramp.rgb[i][0] + (ramp.rgb[i + 1][0] - ramp.rgb[i][0]) * t;
    g = ramp.rgb[i][1] + (ramp.rgb[i + 1][1] - ramp.rgb[i][1]) * t;
    b = ramp.rgb[i][2] + (ramp.rgb[i + 1][2] - ramp.rgb[i][2]) * t;
}

static gfxPackedColor sCarShade(const Vector3 &p, const Vector3 &n, float ao, const gfxModelMaterial &m, const sCarShadeEnv &e) {
    const u32 c = m.car_color;
    float br = ((c >> 16) & 0xff) / 255.0f, bg = ((c >> 8) & 0xff) / 255.0f, bb = (c & 0xff) / 255.0f;
    float alpha = ((c >> 24) & 0xff) / 255.0f;

    Vector3 V = e.camPos - p;
    float vl = V.Mag();
    if (vl > 1e-6f) V.Scale(1.0f / vl); else V.Set(0.0f, 1.0f, 0.0f);
    float NdotL = n.Dot(e.lightDir); if (NdotL < 0.0f) NdotL = 0.0f;
    NdotL *= e.keyScale;
    float NdotV = n.Dot(V); if (NdotV < 0.0f) NdotV = 0.0f;
    float hemi = (0.32f + 0.30f * (n.y * 0.5f + 0.5f)) * e.ambScale;
    float fres = 1.0f - NdotV; fres *= fres; fres *= fres;
    Vector3 H = e.lightDir + V; H.Normalize();
    float NdotH = n.Dot(H); if (NdotH < 0.0f) NdotH = 0.0f;
    // PC PORT: one highlight per car light (see sCarShadeEnv::SpecLight).  DrawHilight
    // scaled a point light's highlight by 1 - dist/25 (its alpha); it measured from the
    // car's origin, this measures per vertex, which over a 4 m car is the same ramp.
    // The render state carries colours at half scale, so the sum is read back up the way
    // the key light is.
    float specR = 0.0f, specG = 0.0f, specB = 0.0f;
    for (int li = 0; li < e.numSpecLights; li++) {
        const sCarShadeEnv::SpecLight &sl = e.specLights[li];
        Vector3 L = sl.dirOrPos;
        float fall = 1.0f;
        if (sl.isPoint) {
            L = sl.dirOrPos - p;
            float d = L.Mag();
            if (d < 1e-4f) continue;
            L.Scale(1.0f / d);
            fall = 1.0f - sClamp01(d / 25.0f);
            if (fall <= 0.0f) continue;
        }
        float ndl = n.Dot(L);
        Vector3 HL = L + V;
        HL.Normalize();
        float ndh = n.Dot(HL);
        if (ndh <= 0.0f) continue;
        float s = m.car_spec * powf(ndh, m.car_gloss) * fall * (ndl > 0.0f ? 1.0f : 0.3f) * 1.6f;
        specR += s * sl.color.x; specG += s * sl.color.y; specB += s * sl.color.z;
    }
    // the scalar the glass alpha and the ambient-only fallback use
    float spec = specR * 0.3f + specG * 0.59f + specB * 0.11f;
    if (e.numSpecLights == 0)
        spec = m.car_spec * powf(NdotH, m.car_gloss) * (NdotL > 0.0f ? 1.0f : 0.3f) * (0.5f + 0.5f * e.keyScale);
    Vector3 R = n * (2.0f * n.Dot(V)) - V;
    float er, eg, eb;
    if (!sCarEnvFromProbe(e, R, er, eg, eb))
        sCarEnvColor(R, er, eg, eb);
    er *= e.ambScale; eg *= e.ambScale; eb *= e.ambScale;

    float r, g, b;
    if (m.car_emissive) {
        float k = 0.8f + 0.2f * hemi;
        r = br * k; g = bg * k; b = bb * k;
    } else if (m.car_metallic) {
        if (m.car_chrome && !m.texture_name.empty() && !m.car_cutout && m.car_reflect >= 0.9f) {
            // reflection comes from the bound environment texture (texgen); the
            // vertex colour only modulates it with key light + ambient
            float diff = 0.70f * e.ambScale + 0.30f * NdotL;
            r = br * diff * ao; g = bg * diff * ao; b = bb * diff * ao;
        } else {
            float k = m.car_reflect * (0.6f + 0.4f * ao);
            r = br * (er * k + hemi * 0.25f + 0.35f * NdotL);
            g = bg * (eg * k + hemi * 0.25f + 0.35f * NdotL);
            b = bb * (eb * k + hemi * 0.25f + 0.35f * NdotL);
        }
    } else if (m.car_paint && e.paintRamp) {
        // The console carpaint pass (see gfxCarPaintRamp): the paint map through
        // reflection texgen, modulate2x with the lit vertex colour.  The map radius is
        // how far the surface turns away from the viewer, so faces read the start of the
        // ramp (the metallic stop's bright saturated colour) and silhouettes the dark base.
        float u = 1.0f - NdotV * NdotV;
        u = u > 0.0f ? sqrtf(u) : 0.0f;
        float pr, pg, pb;
        sCarPaintRampAt(*e.paintRamp, u, pr, pg, pb);
        float k = 2.0f * (hemi + 0.85f * NdotL) * ao;
        float coat = m.car_reflect * (0.15f + 0.85f * fres);
        if (e.envPerPixel) { r = pr * k; g = pg * k; b = pb * k; alpha = coat; }
        else { r = pr * k + er * coat; g = pg * k + eg * coat; b = pb * k + eb * coat; }
    } else {
        float k = (hemi + 0.85f * NdotL) * ao;
        // A lit lens.  The original light shaders (drwShaderTaillight and
        // friends) lerp the light group's AMBIENT toward white by the light's
        // emissive value, which on this per-vertex model means driving the
        // diffuse term to 1 - so an unlit lens stays an ordinary lit surface
        // and a lit one washes out to its base colour.
        if (e.carLight > 0.0f)
            k += (1.0f - k) * sClamp01(e.carLight);
        float coat = m.car_reflect * (0.15f + 0.85f * fres);
        if (e.envPerPixel) { r = br * k; g = bg * k; b = bb * k; alpha = coat; }
        else { r = br * k + er * coat; g = bg * k + eg * coat; b = bb * k + eb * coat; }
    }
    float skScale = 0.5f + 0.5f * ao;
    if (e.numSpecLights > 0) {
        r += specR * skScale; g += specG * skScale; b += specB * skScale;
    } else {
        float sk = spec * skScale;
        r += sk; g += sk; b += sk;
    }
    // drwShaderHeadlight is the one light shader that also drives its base
    // colour ALPHA from the light value (mkfrgba(1,1,1,emVal)): the lit element
    // fades in over the chrome reflector modelled behind it, instead of sitting
    // there as a permanent white panel.  The tail/brake/reverse shaders use
    // mkfrgb and stay opaque, so they keep their own alpha.
    if (m.car_light == CAR_LIGHT_HEAD) alpha = sClamp01(e.carLight);
    if (m.car_glass) alpha = sClamp01(alpha + m.car_glass_fres * fres + 0.6f * spec);
    return mkrgba((u8)(sClamp01(r) * 255.0f), (u8)(sClamp01(g) * 255.0f), (u8)(sClamp01(b) * 255.0f), (u8)(alpha * 255.0f));
}

// The vertex colour of a per-pixel car packet: its occlusion, nothing else.
static inline gfxPackedColor sCarAoColor(float ao) {
    const u8 v = (u8)(sClamp01(ao) * 255.0f);
    return mkrgba(v, v, v, 255);
}

// sCarShade's inputs for the pixel shader (vglCarShadeParams): the same material and
// light terms, so a packet looks as it did per vertex, only resolved per pixel.
static void sCarShadeParamsFor(const gfxModelMaterial &m, const sCarShadeEnv &e, vglCarShadeParams &p) {
    memset(&p, 0, sizeof(p));
    const u32 c = m.car_color;
    p.misc[0] = 1.0f;
    p.misc[1] = m.car_spec;
    p.misc[2] = m.car_gloss;
    p.misc[3] = m.car_reflect;
    p.base[0] = ((c >> 16) & 0xff) / 255.0f;
    p.base[1] = ((c >> 8) & 0xff) / 255.0f;
    p.base[2] = (c & 0xff) / 255.0f;
    p.base[3] = ((c >> 24) & 0xff) / 255.0f;
    p.cam[0] = e.camPos.x; p.cam[1] = e.camPos.y; p.cam[2] = e.camPos.z; p.cam[3] = e.ambScale;
    p.key[0] = e.lightDir.x; p.key[1] = e.lightDir.y; p.key[2] = e.lightDir.z; p.key[3] = e.keyScale;
    const int nSpec = e.numSpecLights < 3 ? e.numSpecLights : 3;
    p.misc2[0] = (float)nSpec;
    static int sDebug = -1;
    if (sDebug < 0) { const char *d = NULL; sDebug = (ARGS.Get("carshadedebug", 0, &d) && d) ? atoi(d) : 0; }
    p.misc2[2] = (float)sDebug;
    for (int li = 0; li < nSpec; li++) {
        const sCarShadeEnv::SpecLight &sl = e.specLights[li];
        p.specDir[li][0] = sl.dirOrPos.x; p.specDir[li][1] = sl.dirOrPos.y; p.specDir[li][2] = sl.dirOrPos.z;
        p.specDir[li][3] = sl.isPoint ? 1.0f : 0.0f;
        p.specCol[li][0] = sl.color.x; p.specCol[li][1] = sl.color.y; p.specCol[li][2] = sl.color.z;
    }
    if (m.car_paint && e.paintRamp && e.paintRamp->count >= 2) {
        const gfxCarPaintRamp &r = *e.paintRamp;
        const int n = r.count < gfxCarPaintRamp::kMaxStops ? r.count : (int)gfxCarPaintRamp::kMaxStops;
        p.misc2[1] = (float)n;
        for (int i = 0; i < n; i++) {
            p.ramp[i][0] = r.rgb[i][0]; p.ramp[i][1] = r.rgb[i][1]; p.ramp[i][2] = r.rgb[i][2];
            p.ramp[i][3] = (float)r.pos[i] / 255.0f;
        }
    }
}

int gfxModel::GetMaxBoneIndex() const {
    int maxBone = 0;
    for (int i = 0; i < packets.GetCount(); i++)
        for (int b = 0; b < packets[i].bone_map.GetCount(); b++)
            if ((int)packets[i].bone_map[b] > maxBone) maxBone = (int)packets[i].bone_map[b];
    return maxBone;
}

void gfxModel::SetCarPaint(u32 rgba) {
    for (int i = 0; i < materials.GetCount(); i++)
        if (materials[i].car_paint) materials[i].car_color = rgba;
}

// The emissive value each of MC3's light shaders computes in its Bind
// (mc3/src/mcgfx/mcShader.c): the running lights only ever push a lens half way,
// the brakes add the other half, and headlights and reverse lights go the whole
// way on their own.
float gfxModel::GetCarLightLevel(const gfxModelMaterial &m) const {
    // -headlightsoff holds the forward lights dark, so the unlit lens can be
    // reached from a command line (the bank's per-car "headlights on" toggle
    // needs a person).  Only the lens shading is affected here; the car's glow
    // sprites are its own business.
    static const bool sNoHeadlights = ARGS.Get("headlightsoff");
    const float head = sNoHeadlights ? 0.0f : m_CarLights.head;
    const float tail = sNoHeadlights ? 0.0f : m_CarLights.tail;
    switch (m.car_light) {
    case CAR_LIGHT_HEAD:       return head;
    case CAR_LIGHT_TAIL:       return tail * 0.5f;
    case CAR_LIGHT_BRAKE:      return tail * 0.5f + m_CarLights.brake * 0.5f;
    case CAR_LIGHT_THIRDBRAKE: return m_CarLights.thirdBrake;
    case CAR_LIGHT_REVERSE:    return m_CarLights.reverse;
    default:                   return 0.0f;
    }
}

void gfxModel::Draw(Matrix44 *matrices, int pass, const atBitSet *enables, int cpvIndex) {
  if (sModelDiagSkip(this, "m44")) return;
    m_DrawPassFilter = pass;
    const atArray<gfxPackedColor> &colorArray = (cpvIndex >= 0 && cpvIndex < cpv_sets.GetCount()) ? cpv_sets[cpvIndex] : colors;
    if (matrices)
        RSTATE.SetIdentity();

    EnumDrawType drawType = (ageRenderMode == renderSolid) ? drawTriangles : drawLine;
    // PC PORT: the caller's depth-write state must survive this draw.  Forcing it
    // back on here made mcSkyHatClass's SetDepthWrite(false) a no-op, so the sky
    // dome (a ~100 m box centred on the camera) wrote depth and every piece of
    // city beyond 100 m failed the depth test -- the whole skyline vanished into
    // a flat band.  Per-material "depthwrite off" still turns it off, never on.
    const bool callerZWrite = RSTATE.GetZWriteEnable();
    const bool callerBlendEnable = RSTATE.GetAlphaBlendEnable();
    const EnumBlendSet callerBlendSet = RSTATE.GetBlendSet();
    const bool callerCustomBlend = (callerBlendSet != blendSet_One_Zero || callerBlendEnable);
    const gfxCullMode callerCull = RSTATE.GetCull();
    const bool callerTexAlphaBlend = RSTATE.GetTextureAlphaBlendAllowed();
    RSTATE.SetZWriteEnable(callerZWrite);
    const bool envMap = m_EnvMap.active;
    const EnumBlendSet envBlendSet = callerBlendSet;
    bool isHDR = false;
    for (int mi = 0; mi < materials.GetCount(); mi++) {
        const char *mn = materials[mi].name.c_str();
        const char *tn = materials[mi].texture_name.c_str();
        if (strstr(mn, "hdr") || strstr(mn, "HDR") || strstr(mn, "cone") || strstr(mn, "beam") || strstr(mn, "flare") || strstr(mn, "Flare") ||
            strstr(tn, "hdr") || strstr(tn, "HDR") || strstr(tn, "cone") || strstr(tn, "beam") || strstr(tn, "flare") || strstr(tn, "Flare")) {
            isHDR = true;
            break;
        }
    }

    sCarShadeEnv carEnv;
    bool carEnvReady = false;
    for (int mi = 0; mi < materials.GetCount() && !carEnvReady; mi++)
        if (materials[mi].car_shade) {
            sCarShadeEnvSetup(carEnv);
            carEnvReady = true;
            // Anchors the -shotrace clock: the first car-shaded draw is the
            // earliest frame the race scene is on screen (see rgl.cpp).
            gfxNoteCarShadedDraw();
        }
    carEnv.paintRamp = m_CarPaintRamp.count >= 2 ? &m_CarPaintRamp : nullptr;
    GPU_SCOPE_IF(carEnvReady, "draw:CarModel");   // -gputime: cars costed apart from the city

    const int nPackets = packets.GetCount();
    for (int pi = 0; pi < nPackets; pi++) {
        const auto &packet = packets[PacketDrawIndex(pi)];
        if (enables && packet.bone_map.GetCount() > 0 && packet.bone_map[0] > 0) {
            if (!enables->IsSet(packet.bone_map[0])) {
                continue;
            }
        }
        gfxTexture *tex = NULL;
        float matDiffR = 1.0f, matDiffG = 1.0f, matDiffB = 1.0f;
        if (packet.material_index < (u32)materials.GetCount()) {
            const auto &mat = materials[packet.material_index];
            if (!mat.texture_name.empty()) {
                tex = gfxGetTexture(mat.texture_name.c_str(), true, false);
            }
            matDiffR = mat.diffuse[0];
            matDiffG = mat.diffuse[1];
            matDiffB = mat.diffuse[2];
            gfxMaterial gfxMat;
            gfxMat.diffuse = Vector4(matDiffR, matDiffG, matDiffB, 1.0f);
            gfxMat.emissive = Vector4(0.0f, 0.0f, 0.0f, 1.0f);
            RSTATE.SetMaterial(&gfxMat);
        }
        bool applyMatColor = (matDiffR != 1.0f || matDiffG != 1.0f || matDiffB != 1.0f);

        // PC PORT: per-material render bucket — when a pass filter is
        // active, a packet draws only in its material's drawbucket
        // (unflagged = bucket 0, the opaque pass).
        if (m_DrawPassFilter >= 0 && packet.material_index < (u32)materials.GetCount()) {
            int b = materials[packet.material_index].drawbucket;
            if (b < 0) b = 0;
            if (b != m_DrawPassFilter) continue;
        }
        bool hasAlpha = (tex && tex->HasAlpha());
        // PC PORT: vehicle materials shade per vertex (below); the fixed lighting
        // and the material diffuse stay out of it
        const gfxModelMaterial *carMat = NULL;
        if (packet.material_index < (u32)materials.GetCount() && materials[packet.material_index].car_shade) {
            carMat = &materials[packet.material_index];
            if (carMat->car_cutout && !tex) continue;            // image-only material without its image
            if (!carMat->car_blend && !carMat->car_cutout && ((carMat->car_color >> 24) & 0xff) == 0 && !tex) continue;
            applyMatColor = false;
            if (carMat->car_blend) hasAlpha = true;
            carEnv.carLight = GetCarLightLevel(*carMat);
        }
        // PC PORT: face a billboard material at the camera, the way
        // mcShaderFlareTexscroll::Draw does - keep the instance's up axis (b),
        // point c at the camera and rebuild a from the two, so the quad turns
        // about its own vertical instead of lying at its authored angle.
        Matrix34 savedWorld;
        bool didBillboard = false;
        if (!matrices && packet.material_index < (u32)materials.GetCount() &&
            materials[packet.material_index].billboard) {
            savedWorld = RSTATE.GetWorld();
            Matrix34 bb = savedWorld;
            bb.c = RSTATE.GetCameraPosition() - bb.d;
            if (bb.c.Mag2() > 1e-6f) {
                bb.c.Normalize();
                bb.a.Cross(bb.c, bb.b);
                if (bb.a.Mag2() > 1e-6f) {
                    bb.a.Normalize();
                    bb.c.Cross(bb.a, bb.b);
                    bb.c.Normalize();
                    RSTATE.SetWorld(bb);
                    didBillboard = true;
                }
            }
        }
        // PC PORT: honor the material .shader's "lighting none" /
        // "depthwrite off" (sMaterialFromShader) - FX materials like the
        // trash piles' BCSteam are unlit non-depth-writing translucents;
        // drawing them lit and depth-writing left milky opaque sheets
        // around the M03 pile bases.
        bool unlit = false, noZWrite = false, additive = false;
        if (packet.material_index < (u32)materials.GetCount()) {
            unlit = materials[packet.material_index].unlit;
            noZWrite = materials[packet.material_index].no_zwrite;
            additive = materials[packet.material_index].additive;
        }
        if (carMat) { unlit = true; if (carMat->car_blend) noZWrite = true; }
        // PC PORT: vehicle geometry draws two-sided.  The GS has no backface
        // culling, so the packs' car strips were never wound consistently -
        // -windinglog measures ~40% of every car part's triangles facing
        // opposite their own vertex normals, spread evenly over body, bumpers
        // and lights alike.  On solid bodywork a reversed triangle is masked by
        // the panel behind it, but a thin single-sided piece (a spoiler wing, a
        // lens cover) simply vanishes from the side it should be seen from - the
        // player's spoiler was visible from below and gone from above.  rscview
        // renders these packs with culling off and gets them right; match it,
        // and only for car_shade materials so city and props keep their culling.
        RSTATE.SetCull(carMat ? cullNone : callerCull);
        // PC PORT: an all-translucent texture means an unlit FX sheet - except on city
        // materials, whose alpha is a wet-reflection mask (sidewalks, grass, asphalt: no
        // fully opaque texel) and whose draw state is the PS2 pass/template (city_blend).
        // Treating those as FX sheets drew the ground unlit, without depth, and blended
        // to nothing, so the sky / reflections showed through where sidewalks should be.
        // Solid car bodywork is the same story from the other end: "all translucent" is
        // measured on a PC alpha scale, but the packs are GS data where 0x80 already means
        // opaque, so a perfectly solid map like fx_carbon_fiber (its CLUT alpha tops out
        // at 133) reads as an FX sheet.  A car pack's shader template, not its texture,
        // says what blends (car_blend / car_cutout), so trust that here - rscview's opaque
        // pass does the same.  Left to the heuristic the stock spoiler drew blended with
        // no depth in the middle of the opaque bucket, and the trunk deck behind it, drawn
        // later, painted straight over it: the wing was there from below (only the city,
        // already drawn, is behind it that way) and gone from above.
        const bool carSolid = carMat && !carMat->car_blend && !carMat->car_cutout;
        if (tex && tex->IsAllTranslucent() && !carSolid &&
            !(packet.material_index < (u32)materials.GetCount() && materials[packet.material_index].city_blend >= 0)) {
            unlit = true;
            noZWrite = true;
            hasAlpha = true;
        }
        // ...and rgl's draw-time override (an all-translucent, or merely alpha-bearing,
        // bound texture forces blend on and depth off) has to stay off them too.  rscview
        // guards its opaque pass with exactly this call; the game never did, because
        // rmcCarModelType::DrawType sets an ordinary blend set before every bucket.
        RSTATE.SetTextureAlphaBlendAllowed(carSolid ? false : callerTexAlphaBlend);
        RSTATE.SetLighting(!unlit);
        RSTATE.SetZWriteEnable(callerZWrite && !noZWrite);
        // PC PORT: "slides"/"slidet" sawtooth UV scroll (conveyor belts,
        // drifting steam) via the stage-0 texture matrix.
        {
            float su = 0.0f, sv = 0.0f;
            if (packet.material_index < (u32)materials.GetCount()) {
                su = materials[packet.material_index].scroll_u;
                sv = materials[packet.material_index].scroll_v;
            }
            if (su != 0.0f || sv != 0.0f) {
                float t = TIME.GetElapsedTime();
                Matrix44 tm(Matrix44::I);
                tm.c.x = fmodf(t * su, 1.0f);
                tm.c.y = fmodf(t * sv, 1.0f);
                RSTATE.SetTexMatrix(0, tm);
                RSTATE.SetTexGeneration(0, 2, false, 0, 0);
            } else {
                RSTATE.SetTexGeneration(0, 0, false, 0, 0);
            }
        }
        bool carChromeTexgen = false;
        if (carMat && carMat->car_chrome && tex && !carMat->car_cutout && carMat->car_reflect >= 0.9f) {
            // chrome: the environment texture through the reflection-vector texgen
            RSTATE.SetTexGeneration(0, 0, false, texsrcCameraSpaceReflectionVector, 0);
            carChromeTexgen = true;
            // PC PORT: chrome reflects the ENVIRONMENT.  With no city environment
            // map it had to reflect its own texture, which is why bumpers and
            // exhausts carried the same fixed sheen in every street.  The live map
            // is rendered for exactly this (mceffects/envmap.cpp) and the texgen
            // above generates the sphere-map UVs it expects.  -nocarenvmap keeps
            // the old self-reflection.
            static const bool sNoCarEnvMap = ARGS.Get("nocarenvmap") != NULL;
            if (!sNoCarEnvMap) {
                if (gfxTexture *cityEnv = gfxGetCityEnvProbeTexture()) tex = cityEnv;
            }
        }
        const int cityBlend = (packet.material_index < (u32)materials.GetCount()) ? materials[packet.material_index].city_blend : -1;
        if (cityBlend >= 0 && !isHDR) {
            // PC PORT: city materials take the PS2 pass + template state (rscCityBlendState),
            // never texture alpha.  Alpha test "> 45" on the PS2 scale: city vertex colours
            // carry alpha 0x80, which halves the doubled texture alpha, so the ref stays 45.
            if (cityBlend == 0) {
                RSTATE.SetAlphaBlendEnable(false);
                RSTATE.SetAlphaFunc(alphaAlways);
            } else if (cityBlend == 3) {
                // Water: translucent alpha blend, disable backface culling, no alpha test rejection
                RSTATE.SetAlphaBlendEnable(true);
                RSTATE.SetBlendSet(blendSet_SrcAlpha_InvSrcAlpha);
                RSTATE.SetAlphaFunc(alphaAlways);
                RSTATE.SetCull(cullNone);
            } else {
                RSTATE.SetAlphaBlendEnable(true);
                RSTATE.SetBlendSet(cityBlend == 2 ? blendSet_InvSrcAlpha_SrcAlpha : blendSet_SrcAlpha_InvSrcAlpha);
                RSTATE.SetAlphaFunc(cityBlend == 1 ? alphaGreater : alphaAlways);
                if (cityBlend == 1) RSTATE.SetAlphaRef(45);
            }
        } else if (callerCustomBlend) {
            RSTATE.SetAlphaBlendEnable(true);
            RSTATE.SetBlendSet(callerBlendSet);
            RSTATE.SetAlphaFunc(alphaAlways);
        } else if (isHDR) {
            RSTATE.SetAlphaBlendEnable(true);
            RSTATE.SetBlendSet(additive ? blendSet_SrcAlpha_One : blendSet_SrcAlpha_InvSrcAlpha);
            RSTATE.SetAlphaFunc(alphaGEqual);
            RSTATE.SetAlphaRef(45);
            noZWrite = true;
            unlit = true;
        } else if (additive) {
            // "blendset add": accumulate onto the frame (glowing coals) -
            // no alpha test, blend regardless of texture alpha.
            RSTATE.SetAlphaBlendEnable(true);
            RSTATE.SetBlendSet(blendSet_SrcAlpha_One);
            RSTATE.SetAlphaFunc(alphaAlways);
        } else {
            RSTATE.SetAlphaBlendEnable(hasAlpha);
            if (hasAlpha) {
                RSTATE.SetBlendSet(blendSet_SrcAlpha_InvSrcAlpha);
                RSTATE.SetAlphaFunc(alphaGEqual);
                RSTATE.SetAlphaRef(8);
            } else {
                RSTATE.SetAlphaFunc(alphaAlways);
            }
        }

        if (envMap) {
            // Environment-map pass: reflect the override texture, unlit,
            // through the reflection-vector texgen at the caller's alpha.
            tex = m_EnvMap.tex ? m_EnvMap.tex : const_cast<gfxTexture*>(RSTATE.GetTexture());
            RSTATE.SetLighting(false);
            RSTATE.SetAlphaBlendEnable(true);
            RSTATE.SetBlendSet(envBlendSet);
            RSTATE.SetAlphaFunc(alphaAlways);
            RSTATE.SetTexGeneration(0, 0, false, texsrcCameraSpaceReflectionVector, 0);
        }
        vglBindTexture(tex);
        int tex2mode = 0;
        float tex2ScaleU = 0.0f, tex2ScaleV = 0.0f;   // PC PORT: material tex2_uvscale
        gfxTexture *tex2map = NULL;
        bool tex2Reflect = false;
        float su2 = 0.0f, sv2 = 0.0f;
        if (!envMap && packet.material_index < (u32)materials.GetCount()) {
            const auto &m2 = materials[packet.material_index];
            if (!m2.texture_name2.empty()) {
                tex2map = gfxGetTexture(m2.texture_name2.c_str(), true, false);
                tex2mode = m2.tex2_mode;
                tex2ScaleU = m2.tex2_uvscale[0];
                tex2ScaleV = m2.tex2_uvscale[1];
                tex2Reflect = m2.tex2_reflect;
                su2 = m2.scroll_u2;
                sv2 = m2.scroll_v2;
            }
        }
        // PC PORT: car paint reflects the live city environment map per PIXEL.  The
        // per-vertex shading cannot resolve a reflection - it only has the corners of
        // a triangle - so it works out how reflective the surface is at this angle
        // (fresnel times the material's reflectivity), leaves that in the vertex
        // alpha, and the stage-2 op adds the sampled environment by it.  Only opaque
        // body materials qualify: glass, the light lenses and anything alpha-tested
        // need their alpha for themselves.  -nocarenvmap keeps the old per-vertex tint.
        carEnv.envPerPixel = false;
        if (carMat && !carChromeTexgen && !envMap && !hasAlpha && carMat->car_reflect > 0.0f &&
            !carMat->car_glass && !carMat->car_emissive && !carMat->car_metallic &&
            carMat->car_light == CAR_LIGHT_NONE) {
            static const bool sNoCarEnvMap = ARGS.Get("nocarenvmap") != NULL;
            gfxTexture *cityEnv = sNoCarEnvMap ? NULL : gfxGetCityEnvProbeTexture();
            if (cityEnv) {
                tex2map = cityEnv;
                tex2mode = 4;                 // add stage 2 by the vertex alpha
                tex2ScaleU = tex2ScaleV = 0.0f;
                tex2Reflect = true;
                su2 = sv2 = 0.0f;
                carEnv.envPerPixel = true;
            }
        }
        if (tex2Reflect) {
            RSTATE.SetTexGeneration(1, 0, false, texsrcCameraSpaceReflectionVector, 0);
        } else if (su2 != 0.0f || sv2 != 0.0f) {
            float t = TIME.GetElapsedTime();
            Matrix44 tm2(Matrix44::I);
            tm2.c.x = fmodf(t * su2, 1.0f);
            tm2.c.y = fmodf(t * sv2, 1.0f);
            RSTATE.SetTexMatrix(1, tm2);
            RSTATE.SetTexGeneration(1, 2, false, 0, 0);
        } else {
            RSTATE.SetTexGeneration(1, 0, false, 0, 0);
        }
        vglBindTexture2(tex2map);
        vglTex2Combine(tex2mode);
        // PC PORT: ...and the rest of sCarShade moves per pixel with it (see
        // vglCarShadeParams): the paint ramp, the diffuse and above all the tight
        // highlights, which per vertex smeared into wedges across the long panel
        // triangles.  -carpervertex keeps the per-vertex shading, for A/B shots.
        static const bool sCarPerVertex = ARGS.Get("carpervertex") != NULL;
        bool carPerPixel = carEnv.envPerPixel && !sCarPerVertex;
        // Glass too: its alpha (fresnel + highlight) and its reflection change fastest of
        // all across a window, and per vertex a rear screen - a few big triangles meeting
        // in the middle - came out as flat quadrants.  The shader samples the environment
        // itself and keeps the alpha for the glass.  Headlight glass stays per vertex: its
        // alpha is the light level (sCarShade, CAR_LIGHT_HEAD).
        bool carGlass = false;
        if (!carPerPixel && !sCarPerVertex && carMat && carMat->car_glass && !carChromeTexgen && !envMap &&
            carMat->car_light != CAR_LIGHT_HEAD) {
            static const bool sNoCarEnvMapGlass = ARGS.Get("nocarenvmap") != NULL;
            if (gfxTexture *cityEnv = sNoCarEnvMapGlass ? NULL : gfxGetCityEnvProbeTexture()) {
                vglBindTexture2(cityEnv);
                carPerPixel = carGlass = true;
            }
        }
        if (carPerPixel) {
            vglCarShadeParams cp;
            sCarShadeParamsFor(*carMat, carEnv, cp);
            if (carGlass) { cp.glass[0] = 1.0f; cp.glass[1] = carEnv.carLight; cp.glass[2] = carMat->car_glass_fres; }
            vglSetCarShade(&cp);
        }

        // Skin the packet before the strips walk it.
        static atArray<Vector3> sSkinPos, sSkinNrm;
        sSkinPacketAdjuncts(packet, matrices, vertices, normals, sSkinPos, sSkinNrm);

        vglBegin(drawType, 0);

        for (const auto &strip : packet.strips) {
            if (strip.indices.GetCount() < 3) continue;

            for (int j = 0; j <= strip.indices.GetCount() - 3; ++j) {
                uint32_t idx0, idx1, idx2;
                bool swap = (j % 2 != 0);
                if (strip.type == 2) {
                    swap = !swap;
                }

                if (swap) {
                    idx0 = strip.indices[j + 1];
                    idx1 = strip.indices[j];
                    idx2 = strip.indices[j + 2];
                } else {
                    idx0 = strip.indices[j];
                    idx1 = strip.indices[j + 1];
                    idx2 = strip.indices[j + 2];
                }

                if (idx0 >= (u32)packet.adjuncts.GetCount() ||
                    idx1 >= (u32)packet.adjuncts.GetCount() ||
                    idx2 >= (u32)packet.adjuncts.GetCount()) {
                    continue;
                }

                const auto &adj0 = packet.adjuncts[idx0];
                const auto &adj1 = packet.adjuncts[idx1];
                const auto &adj2 = packet.adjuncts[idx2];

                if (adj0.vertex_idx >= (u32)vertices.GetCount() ||
                    adj1.vertex_idx >= (u32)vertices.GetCount() ||
                    adj2.vertex_idx >= (u32)vertices.GetCount()) {
                    continue;
                }

                Vector3 p0 = sSkinPos[idx0];
                Vector3 p1 = sSkinPos[idx1];
                Vector3 p2 = sSkinPos[idx2];
                Vector3 n0 = sSkinNrm[idx0];
                Vector3 n1 = sSkinNrm[idx1];
                Vector3 n2 = sSkinNrm[idx2];
                // PC PORT: not for vehicle packs.  -nrmmode's default hands back the raw,
                // unrotated normal because the PS2 .mod characters store theirs in model
                // space; a car pack's NrmAdc normals are bone-local like its positions
                // (they agree with the bone-local faces at |cos| ~0.96), so the skinned
                // normal above is already right and the raw one points the wrong way on
                // every part a bone turns.
                if (matrices && !carMat) {
                    uint32_t h0 = 0, h1 = 0, h2 = 0;
                    if (adj0.bone_idx < (u32)packet.bone_map.GetCount()) h0 = packet.bone_map[adj0.bone_idx];
                    if (adj1.bone_idx < (u32)packet.bone_map.GetCount()) h1 = packet.bone_map[adj1.bone_idx];
                    if (adj2.bone_idx < (u32)packet.bone_map.GetCount()) h2 = packet.bone_map[adj2.bone_idx];
                    sNormalModeFixup(n0, n1, n2, matrices[h0], matrices[h1], matrices[h2],
                                     normals, adj0.normal_idx, adj1.normal_idx, adj2.normal_idx);
                }
                // Neutral, not white: see the GS modulate rule above.
                gfxPackedColor c0 = 0x80808080u;
                gfxPackedColor c1 = 0x80808080u;
                gfxPackedColor c2 = 0x80808080u;

                if (adj0.color_idx < (u32)colorArray.GetCount()) c0 = colorArray[adj0.color_idx];
                if (adj1.color_idx < (u32)colorArray.GetCount()) c1 = colorArray[adj1.color_idx];
                if (adj2.color_idx < (u32)colorArray.GetCount()) c2 = colorArray[adj2.color_idx];

                if ((c0 & 0xff000000u) == 0) c0 |= 0xff000000u;
                if ((c1 & 0xff000000u) == 0) c1 |= 0xff000000u;
                if ((c2 & 0xff000000u) == 0) c2 |= 0xff000000u;
                if (cityBlend == 3) {
                    c0 = (c0 & 0x00ffffffu) | 0xff000000u;
                    c1 = (c1 & 0x00ffffffu) | 0xff000000u;
                    c2 = (c2 & 0x00ffffffu) | 0xff000000u;
                }

                if (applyMatColor) {
                    c0 = sModulateColor(c0, matDiffR, matDiffG, matDiffB);
                    c1 = sModulateColor(c1, matDiffR, matDiffG, matDiffB);
                    c2 = sModulateColor(c2, matDiffR, matDiffG, matDiffB);
                }
                if (carMat) {
                    // the pack's baked vertex colour (0x80 = 1.0) is occlusion
                    Vector3 wp0 = p0, wp1 = p1, wp2 = p2, wn0 = n0, wn1 = n1, wn2 = n2;
                    if (!matrices) {
                        const Matrix34 &W = RSTATE.GetWorld();
                        W.Transform(p0, wp0); W.Transform(p1, wp1); W.Transform(p2, wp2);
                        W.Transform3x3(n0, wn0); W.Transform3x3(n1, wn1); W.Transform3x3(n2, wn2);
                    }
                    wn0.Normalize(); wn1.Normalize(); wn2.Normalize();
                    if (carPerPixel) {
                        // the pixel shader lights it: the vertex brings the occlusion only
                        c0 = sCarAoColor(sCarAo(c0)); c1 = sCarAoColor(sCarAo(c1)); c2 = sCarAoColor(sCarAo(c2));
                    } else {
                        c0 = sCarShade(wp0, wn0, sCarAo(c0), *carMat, carEnv);
                        c1 = sCarShade(wp1, wn1, sCarAo(c1), *carMat, carEnv);
                        c2 = sCarShade(wp2, wn2, sCarAo(c2), *carMat, carEnv);
                    }
                }

                float u0 = 0.0f, v0 = 0.0f;
                float u1 = 0.0f, v1 = 0.0f;
                float u2 = 0.0f, v2 = 0.0f;

                if (adj0.tex1_idx >= 0 && adj0.tex1_idx < tex_coords.GetCount()) { u0 = tex_coords[adj0.tex1_idx].x; v0 = tex_coords[adj0.tex1_idx].y; }
                if (adj1.tex1_idx >= 0 && adj1.tex1_idx < tex_coords.GetCount()) { u1 = tex_coords[adj1.tex1_idx].x; v1 = tex_coords[adj1.tex1_idx].y; }
                if (adj2.tex1_idx >= 0 && adj2.tex1_idx < tex_coords.GetCount()) { u2 = tex_coords[adj2.tex1_idx].x; v2 = tex_coords[adj2.tex1_idx].y; }

                float s0 = 0.0f, t0 = 0.0f;
                float s1 = 0.0f, t1 = 0.0f;
                float s2 = 0.0f, t2 = 0.0f;

                if (adj0.tex2_idx >= 0 && adj0.tex2_idx < tex_coords2.GetCount()) { s0 = tex_coords2[adj0.tex2_idx].x; t0 = tex_coords2[adj0.tex2_idx].y; }
                if (adj1.tex2_idx >= 0 && adj1.tex2_idx < tex_coords2.GetCount()) { s1 = tex_coords2[adj1.tex2_idx].x; t1 = tex_coords2[adj1.tex2_idx].y; }
                if (adj2.tex2_idx >= 0 && adj2.tex2_idx < tex_coords2.GetCount()) { s2 = tex_coords2[adj2.tex2_idx].x; t2 = tex_coords2[adj2.tex2_idx].y; }
                if (tex_coords2.GetCount() == 0) { s0 = u0; t0 = v0; s1 = u1; t1 = v1; s2 = u2; t2 = v2; }
                if (tex2ScaleU != 0.0f) { s0 = u0 * tex2ScaleU; t0 = v0 * tex2ScaleV; s1 = u1 * tex2ScaleU; t1 = v1 * tex2ScaleV; s2 = u2 * tex2ScaleU; t2 = v2 * tex2ScaleV; }

                if (ageRenderMode == renderSolid) {
                    vglTexCoord2f(u0, v0); vglTexCoord2f2(s0, t0); vglColor(c0); vglNormal3f(n0); vglVertex3f(p0);
                    vglTexCoord2f(u1, v1); vglTexCoord2f2(s1, t1); vglColor(c1); vglNormal3f(n1); vglVertex3f(p1);
                    vglTexCoord2f(u2, v2); vglTexCoord2f2(s2, t2); vglColor(c2); vglNormal3f(n2); vglVertex3f(p2);
                } else {
                    vglTexCoord2f(u0, v0); vglTexCoord2f2(s0, t0); vglColor(c0); vglNormal3f(n0); vglVertex3f(p0); vglTexCoord2f(u1, v1); vglTexCoord2f2(s1, t1); vglColor(c1); vglNormal3f(n1); vglVertex3f(p1);
                    vglTexCoord2f(u1, v1); vglTexCoord2f2(s1, t1); vglColor(c1); vglNormal3f(n1); vglVertex3f(p1); vglTexCoord2f(u2, v2); vglTexCoord2f2(s2, t2); vglColor(c2); vglNormal3f(n2); vglVertex3f(p2);
                    vglTexCoord2f(u2, v2); vglTexCoord2f2(s2, t2); vglColor(c2); vglNormal3f(n2); vglVertex3f(p2); vglTexCoord2f(u0, v0); vglTexCoord2f2(s0, t0); vglColor(c0); vglNormal3f(n0); vglVertex3f(p0);
                }
            }
        }

        vglEnd();
        if (carPerPixel) vglSetCarShade(NULL);
        if (didBillboard) RSTATE.SetWorld(savedWorld);
        if (carChromeTexgen) RSTATE.SetTexGeneration(0, 0, false, 0, 0);
        if (carMat && carMat->car_blend) RSTATE.SetZWriteEnable(callerZWrite);
    }
    RSTATE.SetBlendSet(callerBlendSet);
    RSTATE.SetAlphaBlendEnable(callerBlendEnable);
    RSTATE.SetLighting(true);       // PC PORT: undo any per-material unlit
    RSTATE.SetCull(callerCull);     // PC PORT: undo the two-sided car packets
    RSTATE.SetTextureAlphaBlendAllowed(callerTexAlphaBlend);   // PC PORT: undo the opaque-car guard
    RSTATE.SetZWriteEnable(callerZWrite);   // PC PORT: undo any per-material depthwrite off
    RSTATE.SetTexGeneration(0, 0, false, 0, 0);  // PC PORT: undo any UV scroll
    RSTATE.SetTexGeneration(1, 0, false, 0, 0);  // PC PORT: undo stage-2 reflection
    vglTex2Combine(0);
    vglBindTexture2(NULL);
}void gfxModel::DrawSkinned(Matrix34 *matrices) {
  if (sModelDiagSkip(this, "skin")) return;
    m_DrawPassFilter = -1;  // skinned models draw whole, once per frame
    RSTATE.SetIdentity();
    EnumDrawType drawType = (ageRenderMode == renderSolid) ? drawTriangles : drawLine;
    const bool callerZWrite = RSTATE.GetZWriteEnable();   // PC PORT: see gfxModel::Draw
    const bool callerBlendEnable = RSTATE.GetAlphaBlendEnable();
    const EnumBlendSet callerBlendSet = RSTATE.GetBlendSet();
    const bool callerCustomBlend = (callerBlendSet != blendSet_One_Zero || callerBlendEnable);
    const gfxCullMode callerCull = RSTATE.GetCull();
    RSTATE.SetZWriteEnable(callerZWrite);

    const int nPackets = packets.GetCount();
    for (int pi = 0; pi < nPackets; pi++) {
        const auto &packet = packets[PacketDrawIndex(pi)];
        RSTATE.SetCull(callerCull);
        gfxTexture *tex = NULL;
        float matDiffR = 1.0f, matDiffG = 1.0f, matDiffB = 1.0f;
        if (packet.material_index < (u32)materials.GetCount()) {
            const auto &mat = materials[packet.material_index];
            if (!mat.texture_name.empty()) {
                tex = gfxGetTexture(mat.texture_name.c_str(), true, false);
            }
            matDiffR = mat.diffuse[0];
            matDiffG = mat.diffuse[1];
            matDiffB = mat.diffuse[2];
            gfxMaterial gfxMat;
            gfxMat.diffuse = Vector4(matDiffR, matDiffG, matDiffB, 1.0f);
            gfxMat.emissive = Vector4(0.0f, 0.0f, 0.0f, 1.0f);
            RSTATE.SetMaterial(&gfxMat);
        }
        bool applyMatColor = (matDiffR != 1.0f || matDiffG != 1.0f || matDiffB != 1.0f);

        // PC PORT: per-material render bucket — when a pass filter is
        // active, a packet draws only in its material's drawbucket
        // (unflagged = bucket 0, the opaque pass).
        if (m_DrawPassFilter >= 0 && packet.material_index < (u32)materials.GetCount()) {
            int b = materials[packet.material_index].drawbucket;
            if (b < 0) b = 0;
            if (b != m_DrawPassFilter) continue;
        }
        bool hasAlpha = (tex && tex->HasAlpha());
        // PC PORT: honor the material .shader's "lighting none" /
        // "depthwrite off" (sMaterialFromShader) - FX materials like the
        // trash piles' BCSteam are unlit non-depth-writing translucents;
        // drawing them lit and depth-writing left milky opaque sheets
        // around the M03 pile bases.
        bool unlit = false, noZWrite = false, additive = false;
        if (packet.material_index < (u32)materials.GetCount()) {
            unlit = materials[packet.material_index].unlit;
            noZWrite = materials[packet.material_index].no_zwrite;
            additive = materials[packet.material_index].additive;
        }
        // PC PORT: an all-translucent texture means an unlit FX sheet - except on city
        // materials, whose alpha is a wet-reflection mask (sidewalks, grass, asphalt: no
        // fully opaque texel) and whose draw state is the PS2 pass/template (city_blend).
        // Treating those as FX sheets drew the ground unlit, without depth, and blended
        // to nothing, so the sky / reflections showed through where sidewalks should be.
        if (tex && tex->IsAllTranslucent() &&
            !(packet.material_index < (u32)materials.GetCount() && materials[packet.material_index].city_blend >= 0)) {
            unlit = true;
            noZWrite = true;
            hasAlpha = true;
        }
        RSTATE.SetLighting(!unlit);
        RSTATE.SetZWriteEnable(callerZWrite && !noZWrite);
        // PC PORT: "slides"/"slidet" sawtooth UV scroll (conveyor belts,
        // drifting steam) via the stage-0 texture matrix.
        {
            float su = 0.0f, sv = 0.0f;
            if (packet.material_index < (u32)materials.GetCount()) {
                su = materials[packet.material_index].scroll_u;
                sv = materials[packet.material_index].scroll_v;
            }
            if (su != 0.0f || sv != 0.0f) {
                float t = TIME.GetElapsedTime();
                Matrix44 tm(Matrix44::I);
                tm.c.x = fmodf(t * su, 1.0f);
                tm.c.y = fmodf(t * sv, 1.0f);
                RSTATE.SetTexMatrix(0, tm);
                RSTATE.SetTexGeneration(0, 2, false, 0, 0);
            } else {
                RSTATE.SetTexGeneration(0, 0, false, 0, 0);
            }
        }
        const int cityBlend = (packet.material_index < (u32)materials.GetCount()) ? materials[packet.material_index].city_blend : -1;
        if (cityBlend >= 0) {
            // PC PORT: city materials: the PS2 pass + template state, as in gfxModel::Draw
            if (cityBlend == 0) {
                RSTATE.SetAlphaBlendEnable(false);
                RSTATE.SetAlphaFunc(alphaAlways);
            } else if (cityBlend == 3) {
                // Water: translucent alpha blend, disable backface culling, no alpha test rejection
                RSTATE.SetAlphaBlendEnable(true);
                RSTATE.SetBlendSet(blendSet_SrcAlpha_InvSrcAlpha);
                RSTATE.SetAlphaFunc(alphaAlways);
                RSTATE.SetCull(cullNone);
            } else {
                RSTATE.SetAlphaBlendEnable(true);
                RSTATE.SetBlendSet(cityBlend == 2 ? blendSet_InvSrcAlpha_SrcAlpha : blendSet_SrcAlpha_InvSrcAlpha);
                RSTATE.SetAlphaFunc(cityBlend == 1 ? alphaGreater : alphaAlways);
                if (cityBlend == 1) RSTATE.SetAlphaRef(45);
            }
        } else if (callerCustomBlend) {
            RSTATE.SetAlphaBlendEnable(true);
            RSTATE.SetBlendSet(callerBlendSet);
            RSTATE.SetAlphaFunc(alphaAlways);
        } else if (additive) {
            // "blendset add": accumulate onto the frame (glowing coals) -
            // no alpha test, blend regardless of texture alpha.
            RSTATE.SetAlphaBlendEnable(true);
            RSTATE.SetBlendSet(blendSet_SrcAlpha_One);
            RSTATE.SetAlphaFunc(alphaAlways);
        } else {
            RSTATE.SetAlphaBlendEnable(hasAlpha);
            if (hasAlpha) {
                RSTATE.SetBlendSet(blendSet_SrcAlpha_InvSrcAlpha);
                RSTATE.SetAlphaFunc(alphaGEqual);
                RSTATE.SetAlphaRef(8);
            } else {
                RSTATE.SetAlphaFunc(alphaAlways);
            }
        }

        vglBindTexture(tex);
        int tex2mode = 0;
        float tex2ScaleU = 0.0f, tex2ScaleV = 0.0f;   // PC PORT: material tex2_uvscale
        gfxTexture *tex2map = NULL;
        float su2 = 0.0f, sv2 = 0.0f;
        if (packet.material_index < (u32)materials.GetCount()) {
            const auto &m2 = materials[packet.material_index];
            if (!m2.texture_name2.empty()) {
                tex2map = gfxGetTexture(m2.texture_name2.c_str(), true, false);
                tex2mode = m2.tex2_mode;
                tex2ScaleU = m2.tex2_uvscale[0];
                tex2ScaleV = m2.tex2_uvscale[1];
                su2 = m2.scroll_u2;
                sv2 = m2.scroll_v2;
            }
        }
        if (su2 != 0.0f || sv2 != 0.0f) {
            float t = TIME.GetElapsedTime();
            Matrix44 tm2(Matrix44::I);
            tm2.c.x = fmodf(t * su2, 1.0f);
            tm2.c.y = fmodf(t * sv2, 1.0f);
            RSTATE.SetTexMatrix(1, tm2);
            RSTATE.SetTexGeneration(1, 2, false, 0, 0);
        } else {
            RSTATE.SetTexGeneration(1, 0, false, 0, 0);
        }
        vglBindTexture2(tex2map);
        vglTex2Combine(tex2mode);

        // Skin the packet before the strips walk it.
        static atArray<Vector3> sSkinPos, sSkinNrm;
        sSkinPacketAdjuncts(packet, matrices, vertices, normals, sSkinPos, sSkinNrm);

        vglBegin(drawType, 0);

        for (const auto &strip : packet.strips) {
            if (strip.indices.GetCount() < 3) continue;

            for (int j = 0; j <= strip.indices.GetCount() - 3; ++j) {
                uint32_t idx0, idx1, idx2;
                bool swap = (j % 2 != 0);
                if (strip.type == 2) {
                    swap = !swap;
                }

                if (swap) {
                    idx0 = strip.indices[j + 1];
                    idx1 = strip.indices[j];
                    idx2 = strip.indices[j + 2];
                } else {
                    idx0 = strip.indices[j];
                    idx1 = strip.indices[j + 1];
                    idx2 = strip.indices[j + 2];
                }

                if (idx0 >= (u32)packet.adjuncts.GetCount() ||
                    idx1 >= (u32)packet.adjuncts.GetCount() ||
                    idx2 >= (u32)packet.adjuncts.GetCount()) {
                    continue;
                }

                const auto &adj0 = packet.adjuncts[idx0];
                const auto &adj1 = packet.adjuncts[idx1];
                const auto &adj2 = packet.adjuncts[idx2];

                if (adj0.vertex_idx >= (u32)vertices.GetCount() ||
                    adj1.vertex_idx >= (u32)vertices.GetCount() ||
                    adj2.vertex_idx >= (u32)vertices.GetCount()) {
                    continue;
                }

                Vector3 p0 = sSkinPos[idx0];
                Vector3 p1 = sSkinPos[idx1];
                Vector3 p2 = sSkinPos[idx2];
                Vector3 n0 = sSkinNrm[idx0];
                Vector3 n1 = sSkinNrm[idx1];
                Vector3 n2 = sSkinNrm[idx2];
                if (matrices) {
                    uint32_t h0 = 0, h1 = 0, h2 = 0;
                    if (adj0.bone_idx < (u32)packet.bone_map.GetCount()) h0 = packet.bone_map[adj0.bone_idx];
                    if (adj1.bone_idx < (u32)packet.bone_map.GetCount()) h1 = packet.bone_map[adj1.bone_idx];
                    if (adj2.bone_idx < (u32)packet.bone_map.GetCount()) h2 = packet.bone_map[adj2.bone_idx];
                    sNormalModeFixup(n0, n1, n2, matrices[h0], matrices[h1], matrices[h2],
                                     normals, adj0.normal_idx, adj1.normal_idx, adj2.normal_idx);
                }
                // Neutral, not white: see the GS modulate rule above.
                gfxPackedColor c0 = 0xff808080u;
                gfxPackedColor c1 = 0xff808080u;
                gfxPackedColor c2 = 0xff808080u;

                if (adj0.color_idx < (u32)colors.GetCount()) c0 = colors[adj0.color_idx];
                if (adj1.color_idx < (u32)colors.GetCount()) c1 = colors[adj1.color_idx];
                if (adj2.color_idx < (u32)colors.GetCount()) c2 = colors[adj2.color_idx];

                if ((c0 & 0xff000000u) == 0) c0 |= 0xff000000u;
                if ((c1 & 0xff000000u) == 0) c1 |= 0xff000000u;
                if ((c2 & 0xff000000u) == 0) c2 |= 0xff000000u;
                if (cityBlend == 3) {
                    c0 = (c0 & 0x00ffffffu) | 0xff000000u;
                    c1 = (c1 & 0x00ffffffu) | 0xff000000u;
                    c2 = (c2 & 0x00ffffffu) | 0xff000000u;
                }

                if (applyMatColor) {
                    c0 = sModulateColor(c0, matDiffR, matDiffG, matDiffB);
                    c1 = sModulateColor(c1, matDiffR, matDiffG, matDiffB);
                    c2 = sModulateColor(c2, matDiffR, matDiffG, matDiffB);
                }

                float u0 = 0.0f, v0 = 0.0f;
                float u1 = 0.0f, v1 = 0.0f;
                float u2 = 0.0f, v2 = 0.0f;

                if (adj0.tex1_idx >= 0 && adj0.tex1_idx < tex_coords.GetCount()) { u0 = tex_coords[adj0.tex1_idx].x; v0 = tex_coords[adj0.tex1_idx].y; }
                if (adj1.tex1_idx >= 0 && adj1.tex1_idx < tex_coords.GetCount()) { u1 = tex_coords[adj1.tex1_idx].x; v1 = tex_coords[adj1.tex1_idx].y; }
                if (adj2.tex1_idx >= 0 && adj2.tex1_idx < tex_coords.GetCount()) { u2 = tex_coords[adj2.tex1_idx].x; v2 = tex_coords[adj2.tex1_idx].y; }

                float s0 = 0.0f, t0 = 0.0f;
                float s1 = 0.0f, t1 = 0.0f;
                float s2 = 0.0f, t2 = 0.0f;

                if (adj0.tex2_idx >= 0 && adj0.tex2_idx < tex_coords2.GetCount()) { s0 = tex_coords2[adj0.tex2_idx].x; t0 = tex_coords2[adj0.tex2_idx].y; }
                if (adj1.tex2_idx >= 0 && adj1.tex2_idx < tex_coords2.GetCount()) { s1 = tex_coords2[adj1.tex2_idx].x; t1 = tex_coords2[adj1.tex2_idx].y; }
                if (adj2.tex2_idx >= 0 && adj2.tex2_idx < tex_coords2.GetCount()) { s2 = tex_coords2[adj2.tex2_idx].x; t2 = tex_coords2[adj2.tex2_idx].y; }
                if (tex_coords2.GetCount() == 0) { s0 = u0; t0 = v0; s1 = u1; t1 = v1; s2 = u2; t2 = v2; }
                if (tex2ScaleU != 0.0f) { s0 = u0 * tex2ScaleU; t0 = v0 * tex2ScaleV; s1 = u1 * tex2ScaleU; t1 = v1 * tex2ScaleV; s2 = u2 * tex2ScaleU; t2 = v2 * tex2ScaleV; }

                if (ageRenderMode == renderSolid) {
                    vglTexCoord2f(u0, v0); vglTexCoord2f2(s0, t0); vglColor(c0); vglNormal3f(n0); vglVertex3f(p0);
                    vglTexCoord2f(u1, v1); vglTexCoord2f2(s1, t1); vglColor(c1); vglNormal3f(n1); vglVertex3f(p1);
                    vglTexCoord2f(u2, v2); vglTexCoord2f2(s2, t2); vglColor(c2); vglNormal3f(n2); vglVertex3f(p2);
                } else {
                    vglTexCoord2f(u0, v0); vglTexCoord2f2(s0, t0); vglColor(c0); vglNormal3f(n0); vglVertex3f(p0); vglTexCoord2f(u1, v1); vglTexCoord2f2(s1, t1); vglColor(c1); vglNormal3f(n1); vglVertex3f(p1);
                    vglTexCoord2f(u1, v1); vglTexCoord2f2(s1, t1); vglColor(c1); vglNormal3f(n1); vglVertex3f(p1); vglTexCoord2f(u2, v2); vglTexCoord2f2(s2, t2); vglColor(c2); vglNormal3f(n2); vglVertex3f(p2);
                    vglTexCoord2f(u2, v2); vglTexCoord2f2(s2, t2); vglColor(c2); vglNormal3f(n2); vglVertex3f(p2); vglTexCoord2f(u0, v0); vglTexCoord2f2(s0, t0); vglColor(c0); vglNormal3f(n0); vglVertex3f(p0);
                }
            }
        }

        vglEnd();
    }
    RSTATE.SetBlendSet(callerBlendSet);
    RSTATE.SetAlphaBlendEnable(callerBlendEnable);
    RSTATE.SetLighting(true);       // PC PORT: undo any per-material unlit
    RSTATE.SetZWriteEnable(callerZWrite);   // PC PORT: undo any per-material depthwrite off
    RSTATE.SetCull(callerCull);
    RSTATE.SetTexGeneration(0, 0, false, 0, 0);  // PC PORT: undo any UV scroll
    RSTATE.SetTexGeneration(1, 0, false, 0, 0);
    vglBindTexture2(NULL);
}

void gfxModel::GetBoundingBox(Vector3 &min, Vector3 &max) const {
    if (vertices.GetCount() == 0) {
        min.Set(0.0f);
        max.Set(0.0f);
        return;
    }
    min = vertices[0];
    max = vertices[0];
    for (int i = 1; i < vertices.GetCount(); i++) {
        const Vector3 &v = vertices[i];
        if (v.x < min.x) min.x = v.x;
        if (v.y < min.y) min.y = v.y;
        if (v.z < min.z) min.z = v.z;
        if (v.x > max.x) max.x = v.x;
        if (v.y > max.y) max.y = v.y;
        if (v.z > max.z) max.z = v.z;
    }
}

#include "data/timemgr.h"

void gfxModel::Draw(const Matrix34 &mtx, int pass) {
    Draw(mtx, pass, -1);
}

void gfxModel::Draw(const Matrix34 &mtx, int pass, int cpvIndex) {
    m_DrawPassFilter = pass;
    const atArray<gfxPackedColor> &colorArray = (cpvIndex >= 0 && cpvIndex < cpv_sets.GetCount()) ? cpv_sets[cpvIndex] : colors;
  if (sModelDiagSkip(this, "m34")) return;
    unsigned curFrame = TIME.GetFrameCount();
    if (m_LastFrameDrawn == curFrame) {
        return;
    }
    m_LastFrameDrawn = curFrame;

    if (vertices.GetCount() > 0 && PIPE.GetViewport()) {
        Vector3 min, max;
        GetBoundingBox(min, max);
        if (PIPE.GetViewport()->IsAABBVisible(min, max, mtx) == cullOutside) {
            return;
        }
    }

    RSTATE.SetIdentity();
    EnumDrawType drawType = (ageRenderMode == renderSolid) ? drawTriangles : drawLine;
    const bool callerZWrite = RSTATE.GetZWriteEnable();   // PC PORT: see gfxModel::Draw
    const bool callerBlendEnable = RSTATE.GetAlphaBlendEnable();
    const EnumBlendSet callerBlendSet = RSTATE.GetBlendSet();
    const bool callerCustomBlend = (callerBlendSet != blendSet_One_Zero || callerBlendEnable);
    const gfxCullMode callerCull = RSTATE.GetCull();
    bool isHDR = false;
    for (int mi = 0; mi < materials.GetCount(); mi++) {
        const char *mn = materials[mi].name.c_str();
        const char *tn = materials[mi].texture_name.c_str();
        if (strstr(mn, "hdr") || strstr(mn, "HDR") || strstr(mn, "cone") || strstr(mn, "beam") || strstr(mn, "flare") || strstr(mn, "Flare") ||
            strstr(tn, "hdr") || strstr(tn, "HDR") || strstr(tn, "cone") || strstr(tn, "beam") || strstr(tn, "flare") || strstr(tn, "Flare")) {
            isHDR = true;
            break;
        }
    }
    RSTATE.SetZWriteEnable(callerZWrite);

    const int nPackets = packets.GetCount();
    for (int pi = 0; pi < nPackets; pi++) {
        const auto &packet = packets[PacketDrawIndex(pi)];
        RSTATE.SetCull(callerCull);
        gfxTexture *tex = NULL;
        float matDiffR = 1.0f, matDiffG = 1.0f, matDiffB = 1.0f;
        if (packet.material_index < (u32)materials.GetCount()) {
            const auto &mat = materials[packet.material_index];
            if (!mat.texture_name.empty()) {
                tex = gfxGetTexture(mat.texture_name.c_str(), true, false);
            }
            matDiffR = mat.diffuse[0];
            matDiffG = mat.diffuse[1];
            matDiffB = mat.diffuse[2];
            gfxMaterial gfxMat;
            gfxMat.diffuse = Vector4(matDiffR, matDiffG, matDiffB, 1.0f);
            gfxMat.emissive = Vector4(0.0f, 0.0f, 0.0f, 1.0f);
            RSTATE.SetMaterial(&gfxMat);
        }
        bool applyMatColor = (matDiffR != 1.0f || matDiffG != 1.0f || matDiffB != 1.0f);

        // PC PORT: per-material render bucket — when a pass filter is
        // active, a packet draws only in its material's drawbucket
        // (unflagged = bucket 0, the opaque pass).
        if (m_DrawPassFilter >= 0 && packet.material_index < (u32)materials.GetCount()) {
            int b = materials[packet.material_index].drawbucket;
            if (b < 0) b = 0;
            if (b != m_DrawPassFilter) continue;
        }
        bool hasAlpha = (tex && tex->HasAlpha());
        // PC PORT: honor the material .shader's "lighting none" /
        // "depthwrite off" (sMaterialFromShader) - FX materials like the
        // trash piles' BCSteam are unlit non-depth-writing translucents;
        // drawing them lit and depth-writing left milky opaque sheets
        // around the M03 pile bases.
        bool unlit = false, noZWrite = false, additive = false;
        if (packet.material_index < (u32)materials.GetCount()) {
            unlit = materials[packet.material_index].unlit;
            noZWrite = materials[packet.material_index].no_zwrite;
            additive = materials[packet.material_index].additive;
        }
        // PC PORT: an all-translucent texture means an unlit FX sheet - except on city
        // materials, whose alpha is a wet-reflection mask (sidewalks, grass, asphalt: no
        // fully opaque texel) and whose draw state is the PS2 pass/template (city_blend).
        // Treating those as FX sheets drew the ground unlit, without depth, and blended
        // to nothing, so the sky / reflections showed through where sidewalks should be.
        if (tex && tex->IsAllTranslucent() &&
            !(packet.material_index < (u32)materials.GetCount() && materials[packet.material_index].city_blend >= 0)) {
            unlit = true;
            noZWrite = true;
            hasAlpha = true;
        }
        RSTATE.SetLighting(!unlit);
        RSTATE.SetZWriteEnable(callerZWrite && !noZWrite);
        // PC PORT: "slides"/"slidet" sawtooth UV scroll (conveyor belts,
        // drifting steam) via the stage-0 texture matrix.
        {
            float su = 0.0f, sv = 0.0f;
            if (packet.material_index < (u32)materials.GetCount()) {
                su = materials[packet.material_index].scroll_u;
                sv = materials[packet.material_index].scroll_v;
            }
            if (su != 0.0f || sv != 0.0f) {
                float t = TIME.GetElapsedTime();
                Matrix44 tm(Matrix44::I);
                tm.c.x = fmodf(t * su, 1.0f);
                tm.c.y = fmodf(t * sv, 1.0f);
                RSTATE.SetTexMatrix(0, tm);
                RSTATE.SetTexGeneration(0, 2, false, 0, 0);
            } else {
                RSTATE.SetTexGeneration(0, 0, false, 0, 0);
            }
        }
        const int cityBlend = (packet.material_index < (u32)materials.GetCount()) ? materials[packet.material_index].city_blend : -1;
        if (cityBlend >= 0 && !isHDR) {
            // PC PORT: city materials take the PS2 pass + template state (rscCityBlendState),
            // never texture alpha.  Alpha test "> 45" on the PS2 scale: city vertex colours
            // carry alpha 0x80, which halves the doubled texture alpha, so the ref stays 45.
            if (cityBlend == 0) {
                RSTATE.SetAlphaBlendEnable(false);
                RSTATE.SetAlphaFunc(alphaAlways);
            } else if (cityBlend == 3) {
                // Water: translucent alpha blend, disable backface culling, no alpha test rejection
                RSTATE.SetAlphaBlendEnable(true);
                RSTATE.SetBlendSet(blendSet_SrcAlpha_InvSrcAlpha);
                RSTATE.SetAlphaFunc(alphaAlways);
                RSTATE.SetCull(cullNone);
            } else {
                RSTATE.SetAlphaBlendEnable(true);
                RSTATE.SetBlendSet(cityBlend == 2 ? blendSet_InvSrcAlpha_SrcAlpha : blendSet_SrcAlpha_InvSrcAlpha);
                RSTATE.SetAlphaFunc(cityBlend == 1 ? alphaGreater : alphaAlways);
                if (cityBlend == 1) RSTATE.SetAlphaRef(45);
            }
        } else if (callerCustomBlend) {
            RSTATE.SetAlphaBlendEnable(true);
            RSTATE.SetBlendSet(callerBlendSet);
            RSTATE.SetAlphaFunc(alphaAlways);
        } else if (isHDR) {
            RSTATE.SetAlphaBlendEnable(true);
            RSTATE.SetBlendSet(additive ? blendSet_SrcAlpha_One : blendSet_SrcAlpha_InvSrcAlpha);
            RSTATE.SetAlphaFunc(alphaGEqual);
            RSTATE.SetAlphaRef(45);
            noZWrite = true;
            unlit = true;
        } else if (additive) {
            // "blendset add": accumulate onto the frame (glowing coals) -
            // no alpha test, blend regardless of texture alpha.
            RSTATE.SetAlphaBlendEnable(true);
            RSTATE.SetBlendSet(blendSet_SrcAlpha_One);
            RSTATE.SetAlphaFunc(alphaAlways);
        } else {
            RSTATE.SetAlphaBlendEnable(hasAlpha);
            if (hasAlpha) {
                RSTATE.SetBlendSet(blendSet_SrcAlpha_InvSrcAlpha);
                RSTATE.SetAlphaFunc(alphaGEqual);
                RSTATE.SetAlphaRef(8);
            } else {
                RSTATE.SetAlphaFunc(alphaAlways);
            }
        }

        vglBindTexture(tex);
        int tex2mode = 0;
        float tex2ScaleU = 0.0f, tex2ScaleV = 0.0f;   // PC PORT: material tex2_uvscale
        gfxTexture *tex2map = NULL;
        bool tex2Reflect = false;
        float su2 = 0.0f, sv2 = 0.0f;
        if (packet.material_index < (u32)materials.GetCount()) {
            const auto &m2 = materials[packet.material_index];
            if (!m2.texture_name2.empty()) {
                tex2map = gfxGetTexture(m2.texture_name2.c_str(), true, false);
                tex2mode = m2.tex2_mode;
                tex2ScaleU = m2.tex2_uvscale[0];
                tex2ScaleV = m2.tex2_uvscale[1];
                tex2Reflect = m2.tex2_reflect;
                su2 = m2.scroll_u2;
                sv2 = m2.scroll_v2;
            }
        }
        if (tex2Reflect) {
            RSTATE.SetTexGeneration(1, 0, false, texsrcCameraSpaceReflectionVector, 0);
        } else if (su2 != 0.0f || sv2 != 0.0f) {
            float t = TIME.GetElapsedTime();
            Matrix44 tm2(Matrix44::I);
            tm2.c.x = fmodf(t * su2, 1.0f);
            tm2.c.y = fmodf(t * sv2, 1.0f);
            RSTATE.SetTexMatrix(1, tm2);
            RSTATE.SetTexGeneration(1, 2, false, 0, 0);
        } else {
            RSTATE.SetTexGeneration(1, 0, false, 0, 0);
        }
        vglBindTexture2(tex2map);
        vglTex2Combine(tex2mode);
        vglBegin(drawType, 0);

        for (const auto &strip : packet.strips) {
            if (strip.indices.GetCount() < 3) continue;

            for (int j = 0; j <= strip.indices.GetCount() - 3; ++j) {
                uint32_t idx0, idx1, idx2;
                bool swap = (j % 2 != 0);
                if (strip.type == 2) {
                    swap = !swap;
                }

                if (swap) {
                    idx0 = strip.indices[j + 1];
                    idx1 = strip.indices[j];
                    idx2 = strip.indices[j + 2];
                } else {
                    idx0 = strip.indices[j];
                    idx1 = strip.indices[j + 1];
                    idx2 = strip.indices[j + 2];
                }

                if (idx0 >= (u32)packet.adjuncts.GetCount() ||
                    idx1 >= (u32)packet.adjuncts.GetCount() ||
                    idx2 >= (u32)packet.adjuncts.GetCount()) {
                    continue;
                }

                const auto &adj0 = packet.adjuncts[idx0];
                const auto &adj1 = packet.adjuncts[idx1];
                const auto &adj2 = packet.adjuncts[idx2];

                if (adj0.vertex_idx >= (u32)vertices.GetCount() ||
                    adj1.vertex_idx >= (u32)vertices.GetCount() ||
                    adj2.vertex_idx >= (u32)vertices.GetCount()) {
                    continue;
                }

                Vector3 p0 = vertices[adj0.vertex_idx];
                Vector3 p1 = vertices[adj1.vertex_idx];
                Vector3 p2 = vertices[adj2.vertex_idx];

                mtx.Transform(p0, p0);
                mtx.Transform(p1, p1);
                mtx.Transform(p2, p2);

                Vector3 n0(0.0f, 1.0f, 0.0f), n1(0.0f, 1.0f, 0.0f), n2(0.0f, 1.0f, 0.0f);
                if (adj0.normal_idx < (u32)normals.GetCount()) n0 = normals[adj0.normal_idx];
                if (adj1.normal_idx < (u32)normals.GetCount()) n1 = normals[adj1.normal_idx];
                if (adj2.normal_idx < (u32)normals.GetCount()) n2 = normals[adj2.normal_idx];
                n0 = sRotate3x3(mtx, n0); n1 = sRotate3x3(mtx, n1); n2 = sRotate3x3(mtx, n2);
                // Neutral, not white: see the GS modulate rule above.
                gfxPackedColor c0 = 0x80808080u;
                gfxPackedColor c1 = 0x80808080u;
                gfxPackedColor c2 = 0x80808080u;

                if (adj0.color_idx < (u32)colorArray.GetCount()) c0 = colorArray[adj0.color_idx];
                if (adj1.color_idx < (u32)colorArray.GetCount()) c1 = colorArray[adj1.color_idx];
                if ((c0 & 0xff000000u) == 0) c0 |= 0xff000000u;
                if ((c1 & 0xff000000u) == 0) c1 |= 0xff000000u;
                if ((c2 & 0xff000000u) == 0) c2 |= 0xff000000u;
                if (cityBlend == 3) {
                    c0 = (c0 & 0x00ffffffu) | 0xff000000u;
                    c1 = (c1 & 0x00ffffffu) | 0xff000000u;
                    c2 = (c2 & 0x00ffffffu) | 0xff000000u;
                }

                if (applyMatColor) {
                    c0 = sModulateColor(c0, matDiffR, matDiffG, matDiffB);
                    c1 = sModulateColor(c1, matDiffR, matDiffG, matDiffB);
                    c2 = sModulateColor(c2, matDiffR, matDiffG, matDiffB);
                }

                float u0 = 0.0f, v0 = 0.0f;
                float u1 = 0.0f, v1 = 0.0f;
                float u2 = 0.0f, v2 = 0.0f;

                if (adj0.tex1_idx >= 0 && adj0.tex1_idx < tex_coords.GetCount()) { u0 = tex_coords[adj0.tex1_idx].x; v0 = tex_coords[adj0.tex1_idx].y; }
                if (adj1.tex1_idx >= 0 && adj1.tex1_idx < tex_coords.GetCount()) { u1 = tex_coords[adj1.tex1_idx].x; v1 = tex_coords[adj1.tex1_idx].y; }
                if (adj2.tex1_idx >= 0 && adj2.tex1_idx < tex_coords.GetCount()) { u2 = tex_coords[adj2.tex1_idx].x; v2 = tex_coords[adj2.tex1_idx].y; }

                float s0 = 0.0f, t0 = 0.0f;
                float s1 = 0.0f, t1 = 0.0f;
                float s2 = 0.0f, t2 = 0.0f;

                if (adj0.tex2_idx >= 0 && adj0.tex2_idx < tex_coords2.GetCount()) { s0 = tex_coords2[adj0.tex2_idx].x; t0 = tex_coords2[adj0.tex2_idx].y; }
                if (adj1.tex2_idx >= 0 && adj1.tex2_idx < tex_coords2.GetCount()) { s1 = tex_coords2[adj1.tex2_idx].x; t1 = tex_coords2[adj1.tex2_idx].y; }
                if (adj2.tex2_idx >= 0 && adj2.tex2_idx < tex_coords2.GetCount()) { s2 = tex_coords2[adj2.tex2_idx].x; t2 = tex_coords2[adj2.tex2_idx].y; }
                if (tex_coords2.GetCount() == 0) { s0 = u0; t0 = v0; s1 = u1; t1 = v1; s2 = u2; t2 = v2; }
                if (tex2ScaleU != 0.0f) { s0 = u0 * tex2ScaleU; t0 = v0 * tex2ScaleV; s1 = u1 * tex2ScaleU; t1 = v1 * tex2ScaleV; s2 = u2 * tex2ScaleU; t2 = v2 * tex2ScaleV; }

                if (ageRenderMode == renderSolid) {
                    vglTexCoord2f(u0, v0); vglTexCoord2f2(s0, t0); vglColor(c0); vglNormal3f(n0); vglVertex3f(p0);
                    vglTexCoord2f(u1, v1); vglTexCoord2f2(s1, t1); vglColor(c1); vglNormal3f(n1); vglVertex3f(p1);
                    vglTexCoord2f(u2, v2); vglTexCoord2f2(s2, t2); vglColor(c2); vglNormal3f(n2); vglVertex3f(p2);
                } else {
                    vglTexCoord2f(u0, v0); vglTexCoord2f2(s0, t0); vglColor(c0); vglNormal3f(n0); vglVertex3f(p0); vglTexCoord2f(u1, v1); vglTexCoord2f2(s1, t1); vglColor(c1); vglNormal3f(n1); vglVertex3f(p1);
                    vglTexCoord2f(u1, v1); vglTexCoord2f2(s1, t1); vglColor(c1); vglNormal3f(n1); vglVertex3f(p1); vglTexCoord2f(u2, v2); vglTexCoord2f2(s2, t2); vglColor(c2); vglNormal3f(n2); vglVertex3f(p2);
                    vglTexCoord2f(u2, v2); vglTexCoord2f2(s2, t2); vglColor(c2); vglNormal3f(n2); vglVertex3f(p2); vglTexCoord2f(u0, v0); vglTexCoord2f2(s0, t0); vglColor(c0); vglNormal3f(n0); vglVertex3f(p0);
                }
            }
        }

        vglEnd();
    }
    RSTATE.SetBlendSet(callerBlendSet);
    RSTATE.SetAlphaBlendEnable(callerBlendEnable);
    RSTATE.SetLighting(true);       // PC PORT: undo any per-material unlit
    RSTATE.SetZWriteEnable(callerZWrite);   // PC PORT: undo any per-material depthwrite off
    RSTATE.SetCull(callerCull);
    RSTATE.SetTexGeneration(0, 0, false, 0, 0);  // PC PORT: undo any UV scroll
    RSTATE.SetTexGeneration(1, 0, false, 0, 0);  // PC PORT: undo stage-2 reflection
    vglTex2Combine(0);
    vglBindTexture2(NULL);   // don't leak stage 2 into non-model draws
}

void gfxModel::Draw(const rmcShaderGroup &, const rmcShaderData *, int bucket, int, atBitSet *enables, int) const {
    const_cast<gfxModel*>(this)->Draw((Matrix44*)NULL, bucket, enables);
}

void gfxModel::DrawCpv(const rmcShaderGroup &, const rmcShaderData *, int bucket, int, int cpvIndex, int) const {
    const_cast<gfxModel*>(this)->Draw((Matrix44*)NULL, bucket, NULL, cpvIndex);
}

void gfxModel::DrawSkinned(const rmcShaderGroup &, const rmcShaderData *, const Matrix34 *mtxs, int /*mtxCount*/, int, int) const {
    const_cast<gfxModel*>(this)->DrawSkinned(const_cast<Matrix34*>(mtxs));
}

void gfxModel::DrawSkinnedTexturesOnly(Matrix44 *matrices) const {
    if (!matrices) { const_cast<gfxModel*>(this)->DrawSkinned((Matrix34*)NULL); return; }
    int n = MatrixCount > 0 ? MatrixCount : 1;
    std::vector<Matrix34> m34((size_t)n);
    for (int i = 0; i < n; i++) {
        const Matrix44 &m = matrices[i];
        m34[i].a.Set(m.a.x, m.a.y, m.a.z);
        m34[i].b.Set(m.b.x, m.b.y, m.b.z);
        m34[i].c.Set(m.c.x, m.c.y, m.c.z);
        m34[i].d.Set(m.d.x, m.d.y, m.d.z);
    }
    const_cast<gfxModel*>(this)->DrawSkinned(&m34[0]);
}

// Environment-map pass: every packet reflects `tex` (see gfxModel::Draw's
// envMap branch); the caller's blend set and world matrix apply, the vertex
// colours are replaced by white at `alpha` so the reflection strength is the
// caller's to set.
void gfxModel::DrawEnvMapped(int /*lod*/, gfxTexture *tex, float alpha) {
    if (alpha <= 0.0f) return;
    if (alpha > 1.0f) alpha = 1.0f;
    bool lighting = RSTATE.GetLighting();
    m_EnvMap.tex = tex;
    m_EnvMap.alpha = alpha;
    m_EnvMap.active = true;
    RSTATE.SetForceColor((unsigned long)mkfrgba(1.0f, 1.0f, 1.0f, alpha));
    Draw((Matrix44*)NULL, -1);
    RSTATE.SetForceColor(0xffffffffu);   // 0xffffffff = no override
    RSTATE.SetTexGeneration(0, 0, false, 0, 0);
    RSTATE.SetLighting(lighting);
    m_EnvMap.active = false;
    m_EnvMap.tex = NULL;
}

rmcModelGeometry::~rmcModelGeometry() {
    // PC PORT: models from gfxGetModel are cache-owned now - deleting one
    // here would dangle the cache (and every other user of that model).
    // Only a model created outside the cache (e.g. gfxModelFromMesh) is
    // this wrapper's to free.
    if (model && !gfxModelIsCacheOwned(model))
        delete model;
}

void rmcModelGeometry::Draw() const {
    model->Draw();
}

void rmcModelGeometry::DrawSkinned(Matrix34 *matrices) const {
    model->DrawSkinned(matrices);
}

void gfxModelBase::GetBoundingBox(Vector3 &min, Vector3 &max) const {
    for (int i = 0; i < m_ShaderCount; i++) {
        if (m_Geometries[i] && m_Geometries[i]->GetModel()) {
            m_Geometries[i]->GetModel()->GetBoundingBox(min, max);
            return;
        }
    }
    min.Set(0.0f);
    max.Set(0.0f);
}

int gfxModelBase::GetMatrixCount() const {
    for (int i = 0; i < m_ShaderCount; i++) {
        if (m_Geometries[i] && m_Geometries[i]->GetModel()) {
            return m_Geometries[i]->GetModel()->GetMatrixCount();
        }
    }
    return 0;
}

bool gfxModelBase::LoadMod(const char *type, const char *filename, int flags, int fvf, class gfxShaderFactory *factory) {
    gfxModel *mdl = gfxGetModel(filename, flags, fvf);
    if (!mdl) return false;
    m_Geometries[0] = new rmcModelGeometry(mdl);
    m_Shaders[0] = mdl->GetShaders()[0];
    m_ShaderCount = 1;
    return true;
}

