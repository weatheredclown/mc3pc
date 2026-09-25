#include "rmcore/drawable.h"
#include <math.h>
#include "core/stream.h"
#include "data/assetcfg.h"
#include "mesh/serialize.h"
#include "mesh/mesh.h"
#include "data/token.h"
#include "crskeleton/skeldata.h"
#include "gfx/model.h"
#include "core/output.h"
#include "crskeleton/skeleton.h"
#include <cstring>
#include <string>
#include <vector>
#include <cctype>
#include "atl/array.h"
#include "gfx/texture.h"
#include "rmcore/rscgeom.h"
#include "data/rscimage.h"

#include "bank/bank.h"
#include "bank/bkmgr.h"

// ---------------------------------------------------------------------------
// Statics & Bank
// ---------------------------------------------------------------------------

unsigned int rmcDrawable::sm_BucketMask = 0xFFFFFFFF;
bool rmcDrawable::sm_PassEnable[32] = { true };
rmcShaderGroup *rmcDrawable::s_ShaderGroup = nullptr;
int rmcDrawable::s_ShaderGroupCount = 0;

static bool s_DisableCull = false;

#if __BANK
void rmcDrawable::AddWidgets(bkBank &bank) {
    bank.AddToggle("Disable frustum cull", &s_DisableCull);
    bank.AddTitle("Render Bucket Mask");
    bank.AddToggle("Bucket 0 (Opaque)", &sm_BucketMask, 1 << 0);
    bank.AddToggle("Bucket 1 (Alpha Test)", &sm_BucketMask, 1 << 1);
    bank.AddToggle("Bucket 2 (Alpha Blend)", &sm_BucketMask, 1 << 2);
    bank.AddToggle("Bucket 3 (Glow)", &sm_BucketMask, 1 << 3);
    bank.AddToggle("Bucket 4", &sm_BucketMask, 1 << 4);
    bank.AddToggle("Bucket 5", &sm_BucketMask, 1 << 5);
    bank.AddToggle("Bucket 6", &sm_BucketMask, 1 << 6);
    bank.AddToggle("Bucket 7", &sm_BucketMask, 1 << 7);
}
#endif // __BANK

void rmcDrawable::InitBank() {
#if __BANK
    static bool s_BankInited = false;
    if (!s_BankInited) {
        bkBank &bank = BANKMGR.CreateBank("rmcDrawable");
        AddWidgets(bank);
        s_BankInited = true;
    }
#endif
}

// ---------------------------------------------------------------------------
// rmcDrawableBase
// ---------------------------------------------------------------------------

rmcDrawableBase::rmcDrawableBase()
    : m_ShaderGroupCount(0)
    , m_Pad(0)
    , m_Flags(0)
    , m_ShaderGroups(nullptr)
    , m_ShaderData(nullptr)
    , m_ResourceAddr(0)
{
}

rmcDrawableBase::rmcDrawableBase(datResource &rsc)
    : m_ShaderGroupCount(0)
    , m_Pad(0)
    , m_Flags(0)
    , m_ShaderGroups(nullptr)
    , m_ShaderData(nullptr)
    , m_ResourceAddr(0)
{
#if defined(__WIN32PC)
    u32 start = rsc.Tell();
    m_ResourceAddr = start;
    rsc.GetVTable();
    m_ShaderGroupCount = rsc.GetU8();
    m_Pad = rsc.GetU8();
    m_Flags = rsc.GetU16();
    rsc.PointerFixup(m_ShaderGroups);
    rsc.PointerFixup(m_ShaderData);
    u32 consumed = rsc.Tell() - start;
    (void)consumed;
#else
    rsc.PointerFixup(m_ShaderGroups);
    rsc.PointerFixup(m_ShaderData);
#endif
}

rmcDrawableBase::~rmcDrawableBase()
{
}

gfxCullStatus rmcDrawableBase::IsVisible(const Matrix34 &m, const class gfxViewport &vp, u8 &lod, float *dist) const
{
    (void)m; (void)vp; lod = 1; if (dist) *dist = 0.0f;
    return cullInside;
}

// ---------------------------------------------------------------------------
// rmcDrawable
// ---------------------------------------------------------------------------

rmcDrawable::rmcDrawable()
    : rmcDrawableBase()
    , m_LodGroup()
    , m_SkeletonData(nullptr)
    , m_EdgeModel(nullptr)
    , m_Unk_0x70(0)
    , m_EdgeModelSize(0)
{
    InitBank();
}

rmcDrawable::rmcDrawable(datResource &rsc)
    : rmcDrawableBase(rsc)
    , m_LodGroup(rsc)
    , m_SkeletonData(nullptr)
    , m_EdgeModel(nullptr)
    , m_Unk_0x70(0)
    , m_EdgeModelSize(0)
{
#if defined(__WIN32PC)
    u32 start = rsc.Tell();
    // The skeleton has to be BUILT, not just pointed at.  PointerFixup leaves
    // the image address in the field, which is not dereferenceable here, so a
    // skinned drawable out of a resource pack came back with a skeleton that
    // could not be read - and every caller that asks for one (peds through
    // mcCreatureType::Load, the bike rider through mcCharacter) needs it to
    // build its crSkeleton.  crSkeletonData has a resource constructor, so
    // ObjectFixup reads the pointer and constructs the object at that address.
    ObjectFixup(rsc, m_SkeletonData);
    rsc.PointerFixup(m_EdgeModel);
    m_Unk_0x70 = rsc.GetU32();
    m_EdgeModelSize = rsc.GetU32();
    u32 consumed = rsc.Tell() - start;
    (void)consumed;

    // The lod group only carries addresses: the meshes themselves are VIF DMA
    // packets hanging off the lod tables, in the console's own format, so there
    // is nothing to construct - they have to be decoded.  rscLoadDrawableFromResource
    // walks this drawable's lod tables in the image and builds a gfxModel per
    // lod, which is what the car packs already do (mccar/carCustom.cpp).  Until
    // this ran for every resourced drawable, only the callers that knew to ask
    // got geometry, and a ped out of <city>_peds.pck had lod pointers that were
    // image addresses no one ever turned into a model.
    if (rsc.GetImage() && m_ResourceAddr)
        rscLoadDrawableFromResource(*rsc.GetImage(), m_ResourceAddr, this);
#else
    rsc.PointerFixup(m_SkeletonData);
    rsc.PointerFixup(m_EdgeModel);
#endif
}

rmcDrawable::~rmcDrawable()
{
    if (m_SkeletonData) {
        m_SkeletonData->Release();
        m_SkeletonData = nullptr;
    }
}

crSkeletonData* rmcDrawable::GetSkeletonData()
{
    return m_SkeletonData;
}

gfxEdgeModel* rmcDrawable::GetEdgeModel()
{
    return m_EdgeModel;
}

rmcShaderGroup & rmcDrawable::GetShaderGroup(int index) const
{
    if (m_ShaderGroups) return m_ShaderGroups[index];
    if (s_ShaderGroup) return s_ShaderGroup[index];
    static rmcShaderGroup s_default;
    return s_default;
}

rmcShaderData* rmcDrawable::GetShaderData() const
{
    return m_ShaderData;
}

unsigned int rmcDrawable::GetBucketMask() const
{
    return GetBucketMask(0);
}

unsigned int rmcDrawable::GetBucketMask(int pass) const
{
    (void)pass;
    return sm_BucketMask & 1u;
}

gfxModel* rmcDrawable::GetModel(int lod) const
{
    rmcModel *m = m_LodGroup.GetModel(lod);
    return m ? m->GetModel() : nullptr;
}

bool rmcDrawable::Load(const char *filename)
{
    char base[256];
    strncpy(base, filename, sizeof(base) - 1); base[sizeof(base) - 1] = 0;
    size_t n = strlen(base);
    if (n > 5 && _stricmp(base + n - 5, ".type") == 0) base[n - 5] = 0;
    Stream *s = ASSET.Open(base, "type");
    if (!s) { Warningf("rmcDrawable: can't open '%s.type'", base); return false; }
    datTokenizer tok;
    tok.Init(base, s);
    bool ok = Load(tok);
    s->Close();
    return ok;
}

bool rmcDrawable::Load(const char *basename, rmcTypeFileParser *parser, bool configParser)
{
    char base[256];
    strncpy(base, basename, sizeof(base) - 1); base[sizeof(base) - 1] = 0;
    size_t n = strlen(base);
    if (n > 5 && _stricmp(base + n - 5, ".type") == 0) base[n - 5] = 0;
    Stream *s = ASSET.Open(base, "type");
    if (!s) { Warningf("rmcDrawable: can't open '%s.type'", base); return false; }
    datTokenizer tok;
    tok.Init(base, s);
    bool ok = Load(tok, parser, configParser);
    s->Close();
    return ok;
}

bool rmcDrawable::Load(datTokenizer &tok, rmcTypeFileParser *parser, bool configParser)
{
    (void)parser; (void)configParser;
    return Load(tok);
}

void rmcDrawable::LoadMesh(rmcTypeFileCbData *data)
{
    (void)data;
}

void rmcDrawable::LoadSkel(rmcTypeFileCbData *data)
{
    if (!data || !data->m_T) return;
    datTokenizer *T = data->m_T;
    char buf[128];
    T->GetToken(buf, sizeof(buf));
    if (m_SkeletonData) return;
    if (_stricmp(buf, "none") != 0) {
        Stream *S = ASSET.Open(buf, "skel");
        if (S) {
            m_SkeletonData = crSkeletonData::Create(S, buf);
            S->Close();
        }
    }
}

void rmcDrawable::LoadShader(rmcTypeFileCbData *data)
{
    (void)data;
}

void rmcDrawable::LoadEdgeModel(rmcTypeFileCbData *data)
{
    (void)data;
}

bool rmcDrawable::Load(datTokenizer &tok)
{
    char t[256];
    struct ShadingEntry {
        std::string shader;
        std::vector<std::string> texNames;
    };
    std::vector<ShadingEntry> shaders;

    while (!tok.CheckToken("}", false) && tok.GetToken(t, sizeof(t))) {
        if (_stricmp(t, "version:") == 0) {
            tok.GetInt(); // consume version
            continue;
        }

        if (_stricmp(t, "renderable") == 0) {
            tok.MatchToken("{");
            continue;
        }

        if (_stricmp(t, "shadinggroup") == 0) {
            tok.MatchToken("{");
            int depth = 1;
            while (depth > 0 && tok.GetToken(t, sizeof(t))) {
                if (strcmp(t, "{") == 0) {
                    depth++;
                    continue;
                }
                if (strcmp(t, "}") == 0) {
                    depth--;
                    continue;
                }
                if (_stricmp(t, "Shaders") == 0) {
                    int numShaders = tok.GetInt();
                    tok.MatchToken("{");
                    for (int sIdx = 0; sIdx < numShaders; sIdx++) {
                        ShadingEntry entry;
                        tok.GetToken(t, sizeof(t));
                        entry.shader = t;
                        int numTex = tok.GetInt();
                        for (int texIdx = 0; texIdx < numTex; texIdx++) {
                            char texBuf[256];
                            tok.GetToken(texBuf, sizeof(texBuf));
                            entry.texNames.push_back(texBuf);
                        }
                        shaders.push_back(entry);
                    }
                    tok.MatchToken("}");
                }
            }
            continue;
        }

        if (_stricmp(t, "lodgroup") == 0) {
            tok.MatchToken("{");
            int depth = 1;

            auto parseLodEntry = [&](int lodIdx) {
                char countOrName[256];
                tok.GetToken(countOrName, sizeof(countOrName));
                if (_stricmp(countOrName, "none") == 0) {
                    float range = tok.GetFloat();
                    m_LodGroup.m_Thresholds[lodIdx] = range;
                    return;
                }
                struct MeshPart {
                    std::string name;
                    int bone;
                };
                std::vector<MeshPart> parts;
                bool isMultiPart = false;
                if (isdigit((unsigned char)countOrName[0])) {
                    isMultiPart = true;
                    int numParts = atoi(countOrName);
                    for (int pi = 0; pi < numParts; pi++) {
                        char meshName[256];
                        tok.GetToken(meshName, sizeof(meshName));
                        int bone = tok.GetInt();
                        parts.push_back({meshName, bone});
                    }
                } else {
                    // Single model name
                    parts.push_back({countOrName, 0});
                }
                float range = tok.GetFloat();
                m_LodGroup.m_Thresholds[lodIdx] = range;

                gfxModel *composite = nullptr;
                if (!isMultiPart && parts.size() == 1) {
                    // Single model (character or single prop):
                    // Strip .mod extension if present so gfxGetModel / ASSET.Open handles it cleanly
                    char cleanName[256];
                    strncpy(cleanName, parts[0].name.c_str(), sizeof(cleanName) - 1);
                    cleanName[sizeof(cleanName) - 1] = 0;
                    size_t clen = strlen(cleanName);
                    if (clen > 4 && _stricmp(cleanName + clen - 4, ".mod") == 0) cleanName[clen - 4] = 0;

                    composite = gfxGetModel(cleanName, 0);
                    if (!composite) {
                        mshMesh subMesh;
                        if (SerializeFromFile(cleanName, subMesh) || subMesh.LoadMod(cleanName)) {
                            composite = gfxModelFromMesh(subMesh);
                        }
                    }

                    if (composite && !shaders.empty()) {
                        for (int mi = 0; mi < composite->materials.GetCount(); mi++) {
                            auto &mat = composite->materials[mi];
                            if (mat.name.size() > 1 && mat.name[0] == '#') {
                                int sIdx = atoi(mat.name.c_str() + 1);
                                if (sIdx >= 0 && sIdx < (int)shaders.size()) {
                                    const auto &se = shaders[sIdx];
                                    if (!se.texNames.empty() && _stricmp(se.texNames[0].c_str(), "none") != 0) {
                                        mat.texture_name = se.texNames[0];
                                        gfxGetTexture(mat.texture_name.c_str(), true, false);
                                    }
                                    if (se.texNames.size() > 1 && se.texNames[1] != "__envmap__") {
                                        mat.texture_name2 = se.texNames[1];
                                        gfxGetTexture(mat.texture_name2.c_str(), true, false);
                                    }
                                    if (se.shader.find("shadow") != std::string::npos) {
                                        mat.no_zwrite = true;
                                        mat.unlit = true;
                                    }
                                    if (se.shader.find("checkpoint_arrow") != std::string::npos) {
                                        if (se.texNames.size() > 1 && _stricmp(se.texNames[1].c_str(), "none") != 0) {
                                            mat.texture_name = se.texNames[1];
                                            gfxGetTexture(mat.texture_name.c_str(), true, false);
                                        }
                                        mat.scroll_u = 0.8f;
                                        mat.additive = true;
                                        mat.unlit = true;
                                        mat.no_zwrite = true;
                                    }
                                }
                            }
                        }
                    }
                } else {
                    for (const auto &part : parts) {
                        mshMesh subMesh;
                        gfxModel *partModel = nullptr;
                        if (SerializeFromFile(part.name.c_str(), subMesh)) {
                            partModel = gfxModelFromMesh(subMesh);
                        } else if (subMesh.LoadMod(part.name.c_str())) {
                            partModel = gfxModelFromMesh(subMesh);
                        } else {
                            partModel = gfxGetModel(part.name.c_str(), 0);
                        }
                        if (!partModel) continue;

                        // Resolve materials from shaders list if "#<index>"
                        for (int mi = 0; mi < partModel->materials.GetCount(); mi++) {
                            auto &mat = partModel->materials[mi];
                            if (mat.name.size() > 1 && mat.name[0] == '#') {
                                int sIdx = atoi(mat.name.c_str() + 1);
                                if (sIdx >= 0 && sIdx < (int)shaders.size()) {
                                    const auto &se = shaders[sIdx];
                                    if (!se.texNames.empty() && _stricmp(se.texNames[0].c_str(), "none") != 0) {
                                        mat.texture_name = se.texNames[0];
                                        gfxGetTexture(mat.texture_name.c_str(), true, false);
                                    }
                                    if (se.texNames.size() > 1 && se.texNames[1] != "__envmap__") {
                                        mat.texture_name2 = se.texNames[1];
                                        gfxGetTexture(mat.texture_name2.c_str(), true, false);
                                    }
                                    if (se.shader.find("shadow") != std::string::npos) {
                                        mat.no_zwrite = true;
                                        mat.unlit = true;
                                    }
                                    if (se.shader.find("checkpoint_arrow") != std::string::npos) {
                                        if (se.texNames.size() > 1 && _stricmp(se.texNames[1].c_str(), "none") != 0) {
                                            mat.texture_name = se.texNames[1];
                                            gfxGetTexture(mat.texture_name.c_str(), true, false);
                                        }
                                        mat.scroll_u = 0.8f;
                                        mat.additive = true;
                                        mat.unlit = true;
                                        mat.no_zwrite = true;
                                    }
                                }
                            }
                        }

                        // Set bone mapping for each packet
                        for (int pki = 0; pki < partModel->packets.GetCount(); pki++) {
                            auto &pk = partModel->packets[pki];
                            pk.bone_map.Reset();
                            pk.bone_map.Append((u32)part.bone);
                            for (int ai = 0; ai < pk.adjuncts.GetCount(); ai++) {
                                pk.adjuncts[ai].bone_idx = 0;
                            }
                        }

                        if (!composite) {
                            composite = partModel;
                        } else {
                            int vOff = composite->vertices.GetCount();
                            int nOff = composite->normals.GetCount();
                            int cOff = composite->colors.GetCount();
                            int tOff = composite->tex_coords.GetCount();
                            int t2Off = composite->tex_coords2.GetCount();
                            int mOff = composite->materials.GetCount();

                            for (int i = 0; i < partModel->vertices.GetCount(); i++)
                                composite->vertices.Append(partModel->vertices[i]);
                            for (int i = 0; i < partModel->normals.GetCount(); i++)
                                composite->normals.Append(partModel->normals[i]);
                            for (int i = 0; i < partModel->colors.GetCount(); i++)
                                composite->colors.Append(partModel->colors[i]);
                            for (int i = 0; i < partModel->tex_coords.GetCount(); i++)
                                composite->tex_coords.Append(partModel->tex_coords[i]);
                            for (int i = 0; i < partModel->tex_coords2.GetCount(); i++)
                                composite->tex_coords2.Append(partModel->tex_coords2[i]);
                            for (int i = 0; i < partModel->materials.GetCount(); i++)
                                composite->materials.Append(partModel->materials[i]);

                            for (int i = 0; i < partModel->packets.GetCount(); i++) {
                                gfxModelPacket pk = partModel->packets[i];
                                pk.material_index += mOff;
                                for (int a = 0; a < pk.adjuncts.GetCount(); a++) {
                                    pk.adjuncts[a].vertex_idx += vOff;
                                    if (pk.adjuncts[a].normal_idx != 0xFFFFFFFFu) pk.adjuncts[a].normal_idx += nOff;
                                    if (pk.adjuncts[a].color_idx != 0xFFFFFFFFu) pk.adjuncts[a].color_idx += cOff;
                                    if (pk.adjuncts[a].tex1_idx >= 0) pk.adjuncts[a].tex1_idx += tOff;
                                    if (pk.adjuncts[a].tex2_idx >= 0) pk.adjuncts[a].tex2_idx += t2Off;
                                }
                                composite->packets.Append(pk);
                            }
                            delete partModel;
                        }
                    }
                }

                if (composite) {
                    composite->BuildDrawOrder();
                    SetModel(lodIdx, composite);
                    if (composite->vertices.GetCount() > 0) {
                        Vector3 mn = composite->vertices[0], mx = mn;
                        for (int vi = 1; vi < composite->vertices.GetCount(); vi++) {
                            const Vector3 &v = composite->vertices[vi];
                            if (v.x < mn.x) mn.x = v.x; if (v.y < mn.y) mn.y = v.y; if (v.z < mn.z) mn.z = v.z;
                            if (v.x > mx.x) mx.x = v.x; if (v.y > mx.y) mx.y = v.y; if (v.z > mx.z) mx.z = v.z;
                        }
                        if (lodIdx == 0) SetBoundingBox(mn, mx);
                    }
                }
            };

            while (depth > 0 && tok.GetToken(t, sizeof(t))) {
                if (strcmp(t, "{") == 0) {
                    depth++;
                    continue;
                }
                if (strcmp(t, "}") == 0) {
                    depth--;
                    continue;
                }
                if (_stricmp(t, "mesh") == 0) {
                    tok.MatchToken("{");
                    for (;;) {
                        if (!tok.GetToken(t, sizeof(t))) break;
                        if (strcmp(t, "}") == 0) break;

                        int lodIdx = -1;
                        if (_stricmp(t, "high") == 0) lodIdx = 0;
                        else if (_stricmp(t, "med") == 0) lodIdx = 1;
                        else if (_stricmp(t, "low") == 0) lodIdx = 2;
                        else if (_stricmp(t, "vlow") == 0) lodIdx = 3;

                        if (lodIdx >= 0) {
                            parseLodEntry(lodIdx);
                        } else if (_stricmp(t, "center") == 0) {
                            m_LodGroup.m_Center.x = tok.GetFloat();
                            m_LodGroup.m_Center.y = tok.GetFloat();
                            m_LodGroup.m_Center.z = tok.GetFloat();
                        } else if (_stricmp(t, "radius") == 0) {
                            m_LodGroup.m_Radius = tok.GetFloat();
                        }
                    }
                } else if (_stricmp(t, "high") == 0) {
                    parseLodEntry(0);
                } else if (_stricmp(t, "med") == 0) {
                    parseLodEntry(1);
                } else if (_stricmp(t, "low") == 0) {
                    parseLodEntry(2);
                } else if (_stricmp(t, "vlow") == 0) {
                    parseLodEntry(3);
                } else if (_stricmp(t, "center") == 0) {
                    m_LodGroup.m_Center.x = tok.GetFloat();
                    m_LodGroup.m_Center.y = tok.GetFloat();
                    m_LodGroup.m_Center.z = tok.GetFloat();
                } else if (_stricmp(t, "radius") == 0) {
                    m_LodGroup.m_Radius = tok.GetFloat();
                }
            }
            continue;
        }
        else if (_stricmp(t, "skel") == 0) {
            char skelName[256];
            tok.GetToken(skelName, sizeof(skelName));
            if (_stricmp(skelName, "{") == 0) {
                while (tok.GetToken(skelName, sizeof(skelName))) {
                    if (strcmp(skelName, "}") == 0) break;
                    if (_stricmp(skelName, "skel") == 0) {
                        tok.GetToken(skelName, sizeof(skelName));
                        if (_stricmp(skelName, "none") != 0 && !m_SkeletonData) {
                            m_SkeletonData = new crSkeletonData();
                            if (!m_SkeletonData->Load(skelName)) {
                                delete m_SkeletonData;
                                m_SkeletonData = nullptr;
                            }
                        }
                    }
                }
            } else if (_stricmp(skelName, "none") != 0 && !m_SkeletonData) {
                m_SkeletonData = new crSkeletonData();
                if (!m_SkeletonData->Load(skelName)) {
                    delete m_SkeletonData;
                    m_SkeletonData = nullptr;
                }
            }
            continue;
        }
        else if (_stricmp(t, "edge") == 0) {
            char edgeName[256];
            tok.GetToken(edgeName, sizeof(edgeName));
            if (_stricmp(edgeName, "{") == 0) {
                while (tok.GetToken(edgeName, sizeof(edgeName))) {
                    if (strcmp(edgeName, "}") == 0) break;
                    if (_stricmp(edgeName, "none") != 0 && !m_EdgeModel)
                        m_EdgeModel = gfxEdgeModel::Create(edgeName);
                }
            } else if (_stricmp(edgeName, "none") != 0 && !m_EdgeModel) {
                m_EdgeModel = gfxEdgeModel::Create(edgeName);
            }
            continue;
        }

        // Generic block skip (bound, bonetag, animation, etc.) with brace depth tracking
        char next[256];
        if (tok.GetToken(next, sizeof(next))) {
            if (strcmp(next, "{") == 0) {
                int depth = 1;
                while (depth > 0 && tok.GetToken(next, sizeof(next))) {
                    if (strcmp(next, "{") == 0) depth++;
                    else if (strcmp(next, "}") == 0) depth--;
                }
            }
        }
    }

    if (m_LodGroup.m_Radius <= 0.0f) {
        Vector3 ext = m_LodGroup.m_Max - m_LodGroup.m_Min;
        m_LodGroup.m_Radius = ext.Mag() * 0.5f;
        if (m_LodGroup.m_Radius <= 0.0f) m_LodGroup.m_Radius = 4.0f;
    }

    return true;
}

gfxCullStatus rmcDrawable::IsVisible(const Matrix34 &m, const class gfxViewport &vp, u8 &lod, float *dist) const
{
    if (s_DisableCull) {
        lod = 1;
        return cullClipped;
    }
    float zDist = 0.0f;
    gfxCullStatus status = vp.IsSphereVisible(m.d.x, m.d.y, m.d.z, m_LodGroup.m_Radius, &zDist);
    if (status == cullOutside) {
        return cullOutside;
    }

    if (m_LodGroup.m_Radius > 0.0f) {
        float pxRadius = vp.GetProjectedPixelRadius(m_LodGroup.m_Radius, zDist);
        if (pxRadius < 1.0f) {
            return cullOutside;
        }
    }

    if (m_LodGroup.m_Min.x != m_LodGroup.m_Max.x || m_LodGroup.m_Min.y != m_LodGroup.m_Max.y || m_LodGroup.m_Min.z != m_LodGroup.m_Max.z) {
        gfxCullStatus boxStatus = vp.IsAABBVisible(m_LodGroup.m_Min, m_LodGroup.m_Max, m, &zDist);
        if (boxStatus == cullOutside) {
            return cullOutside;
        }
    }

    lod = 1;
    for (int i = 0; i < 4; i++) {
        if (m_LodGroup.GetModel(i)) {
            if (zDist < m_LodGroup.m_Thresholds[i]) {
                lod = i + 1;
                return status;
            }
        }
    }
    lod = 1;
    for (int i = 3; i >= 0; i--) {
        if (m_LodGroup.GetModel(i)) {
            lod = i + 1;
            break;
        }
    }
    return status;
}

gfxCullStatus rmcDrawable::IsVisible(const Matrix34 &m, const class gfxViewport &vp, u8 &lod) const
{
    return IsVisible(m, vp, lod, nullptr);
}

void rmcDrawable::Draw(int pass, rmcShaderData* sd, const Matrix34 &m, int pass2, int flag) const
{
    int activePass = (pass2 >= 0) ? pass2 : pass;
    if (activePass >= 0 && activePass < 32 && ((GetBucketMask() & (1 << activePass)) == 0)) {
        return;
    }

    int lodIndex = flag - 1;
    if (lodIndex < 0 || lodIndex >= 4) lodIndex = 0;
    gfxModel* model = GetModel(lodIndex);
    if (!model && lodIndex != 0) model = GetModel(0);
    if (model) {
        Matrix44 m44;
        m44.a.Set(m.a.x, m.a.y, m.a.z, 0.0f);
        m44.b.Set(m.b.x, m.b.y, m.b.z, 0.0f);
        m44.c.Set(m.c.x, m.c.y, m.c.z, 0.0f);
        m44.d.Set(m.d.x, m.d.y, m.d.z, 1.0f);
        model->Draw(&m44, activePass);
    }
}

void rmcDrawable::DrawSkinned(int pass, rmcShaderData* sd, const crSkeleton &skel, int pass2, int flag) const
{
    if (!skel.HasSkeletonData() || skel.GetBones() == NULL) {
        return;
    }
    int numBones = skel.GetSkeletonData().GetNumBones();
    if (numBones <= 0) {
        return;
    }

    int activePass = (pass2 >= 0) ? pass2 : pass;
    if (activePass >= 0 && activePass < 32 && ((GetBucketMask() & (1 << activePass)) == 0)) {
        return;
    }

    int lodIndex = flag - 1;
    if (lodIndex < 0 || lodIndex >= 4) lodIndex = 0;
    gfxModel* model = GetModel(lodIndex);
    if (!model && lodIndex != 0) model = GetModel(0);
    if (model) {
        atArray<Matrix34> skinnedMatrices;
        skinnedMatrices.Resize(numBones);
        crSkeleton &mutableSkel = const_cast<crSkeleton&>(skel);
        if (model->IsModelRelative())
            mutableSkel.AttachModelRelative(skinnedMatrices.begin());
        else
            mutableSkel.Attach(skinnedMatrices.begin());

        const int matrixCount = model->GetMatrixCount();
        const int firstBone = (matrixCount > 0 && matrixCount == numBones - 1) ? 1 : 0;
        model->DrawSkinned(skinnedMatrices.begin() + firstBone);
    }
}

void rmcDrawable::Draw(const class rmcShaderGroup &shaderGroup, rmcShaderData *sd, int bucket, int arg3) const
{
    if (bucket >= 0 && bucket < 32 && ((GetBucketMask() & (1u << bucket)) == 0)) {
        return;
    }
    for (int i = 0; i < 4; ++i) {
        gfxModel *model = GetModel(i);
        if (model) {
            model->Draw((Matrix44 *)nullptr, bucket);
            return;
        }
    }
}

void rmcDrawable::Draw(const class rmcShaderGroup &shaderGroup, rmcShaderData *sd, const Matrix34 *mtx, int bucket, int lod, atBitSet *enables, int variant) const
{
    if (bucket >= 0 && bucket < 32 && ((GetBucketMask() & (1u << bucket)) == 0)) {
        return;
    }
    gfxModel *model = GetModel(lod);
    if (!model && lod != 0) model = GetModel(0);
    if (!model) return;
    // `mtx` is the first entry of the caller's bone matrix array (the console
    // rmcore contract: rmcCarModelType::DrawType hands over the whole skeleton).
    // A model whose parts sit on several bones is placed by that array; a
    // single-bone model by the one matrix.
    int maxBone = model->GetMaxBoneIndex();
    if (mtx && maxBone > 0 && maxBone < 512) {
        static Matrix44 s_palette[512];
        for (int i = 0; i <= maxBone; i++) {
            const Matrix34 &m = mtx[i];
            s_palette[i].a.Set(m.a.x, m.a.y, m.a.z, 0.0f);
            s_palette[i].b.Set(m.b.x, m.b.y, m.b.z, 0.0f);
            s_palette[i].c.Set(m.c.x, m.c.y, m.c.z, 0.0f);
            s_palette[i].d.Set(m.d.x, m.d.y, m.d.z, 1.0f);
        }
        model->Draw(s_palette, bucket, enables);
        return;
    }
    if (mtx) {
        RSTATE.SetWorld(*mtx);
    }
    model->Draw((Matrix44 *)nullptr, bucket, enables);
}

void rmcDrawable::Draw(const rmcShaderData *data, const Matrix34 &mtx, int bucket, int lod, atBitSet *enables, int variant) const
{
    Draw(GetShaderGroup(), (rmcShaderData*)data, &mtx, bucket, lod, enables, variant);
}

void rmcDrawable::DrawCpv(const rmcShaderData *data, const Matrix34 &mtx, int bucket, int lod, int cpvIndex, int variant) const
{
    (void)lod; (void)cpvIndex; (void)variant;
    Draw(data, mtx, bucket, lod, nullptr, variant);
}

void rmcDrawable::DrawSkinned(const rmcShaderData *data, const crSkeleton &skel, int bucket, int lod, int variant) const
{
    DrawSkinned(0, (rmcShaderData*)data, skel, bucket, lod + 1);
}

void rmcDrawable::DrawSkinned(const rmcShaderGroup &shaderGroup, rmcShaderData *sd, const Matrix34 *mtx, int /*numMtx*/, int bucket, int arg) const
{
    if (mtx) Draw(sd, *mtx, bucket, 0);
    else Draw(shaderGroup, sd, bucket, arg);
}

void rmcDrawable::DrawCpv(const rmcShaderGroup &shaderGroup, rmcShaderData *sd, int bucket, int lod, int cpvIndex) const
{
    (void)lod; (void)cpvIndex;
    Draw(shaderGroup, sd, bucket, 0);
}

// ---------------------------------------------------------------------------
// rmcModel
// ---------------------------------------------------------------------------

rmcModel::rmcModel(unsigned char flags)
    : m_Flags(flags)
    , m_NumPackets(0)
    , m_Flags2(0)
    , m_Packets(nullptr)
{
}

rmcModel::rmcModel(datResource &rsc)
    : m_Flags(0)
    , m_NumPackets(0)
    , m_Flags2(0)
    , m_Packets(nullptr)
{
#if defined(__WIN32PC)
    u32 start = rsc.Tell();
    rsc.GetVTable();
    m_Flags = rsc.GetU32();
    m_NumPackets = rsc.GetU16();
    m_Flags2 = rsc.GetU16();
    rsc.PointerFixup(m_Packets);
    u32 consumed = rsc.Tell() - start;
    (void)consumed;
#else
    rsc.PointerFixup(m_Packets);
#endif
}

rmcModel::~rmcModel()
{
}

void rmcModel::Draw(const rmcShaderGroup &shaders, const rmcShaderData *data, int bucket, int lod) const
{
    (void)shaders; (void)data; (void)lod;
    if (m_GfxModel) {
        m_GfxModel->Draw((Matrix44*)nullptr, bucket);
    }
}

void rmcModel::DrawCpv(const rmcShaderGroup &shaders, const rmcShaderData *data, int bucket, int lod, int cpvIndex) const
{
    if (m_GfxModel) {
        m_GfxModel->DrawCpv(shaders, data, bucket, lod, cpvIndex);
    }
}

void rmcModel::DrawSkinned(const rmcShaderGroup &shaders, const rmcShaderData *data, const Matrix34 *mtxs, int mtxCount, int bucket, int lod, const atBitSet *enables) const
{
    (void)shaders; (void)data; (void)lod;
    if (m_GfxModel) {
        if (mtxs && mtxCount > 0) {
            int maxBone = 0;
            for (int i = 0; i < m_GfxModel->packets.GetCount(); i++) {
                for (int b = 0; b < m_GfxModel->packets[i].bone_map.GetCount(); b++) {
                    int bId = (int)m_GfxModel->packets[i].bone_map[b];
                    if (bId > maxBone) maxBone = bId;
                }
            }
            int count = maxBone + 1;
            if (count > mtxCount) count = mtxCount;
            if (count > 512) count = 512;
            if (count <= 0) count = 1;

            static Matrix44 s_m44[512];
            for (int i = 0; i < count; i++) {
                s_m44[i].a.Set(mtxs[i].a.x, mtxs[i].a.y, mtxs[i].a.z, 0.0f);
                s_m44[i].b.Set(mtxs[i].b.x, mtxs[i].b.y, mtxs[i].b.z, 0.0f);
                s_m44[i].c.Set(mtxs[i].c.x, mtxs[i].c.y, mtxs[i].c.z, 0.0f);
                s_m44[i].d.Set(mtxs[i].d.x, mtxs[i].d.y, mtxs[i].d.z, 1.0f);
            }
            m_GfxModel->Draw(s_m44, bucket, enables);
        } else {
            m_GfxModel->Draw((Matrix44*)nullptr, bucket, enables);
        }
    }
}

unsigned int rmcModel::ComputeBucketMask(const rmcShaderGroup &shaders) const
{
    (void)shaders;
    return 0xFFFFFFFF;
}

unsigned int rmcModel::GetBucketMask() const
{
    return 0xFFFFFFFF;
}

bool rmcModel::Load(const char *name)
{
    m_GfxModel = gfxGetModel(name, 0);
    return m_GfxModel != nullptr;
}

int rmcModel::GetMatrixCount() const
{
    return m_GfxModel ? m_GfxModel->GetMatrixCount() : 0;
}

void rmcModel::SetBoundingBox(const Vector3 &min, const Vector3 &max)
{
    (void)min; (void)max;
}

void rmcModel::GetBoundingBox(Vector3 &min, Vector3 &max) const
{
    if (m_GfxModel) {
        m_GfxModel->GetBoundingBox(min, max);
    } else {
        min.Zero();
        max.Zero();
    }
}

rmcModel* rmcModel::Create(
    rmcShaderGroup &shaderGroup,
    const char *name,
    int arg1,
    int arg2,
    bool arg3,
    rmcModelInfo *info
) {
    (void)shaderGroup; (void)arg1; (void)arg2; (void)arg3; (void)info;
    rmcModel *m = new rmcModel();
    if (!m->Load(name)) { delete m; return nullptr; }
    return m;
}

rmcModel* rmcModel::Create(
    rmcShaderGroup &shaderGroup,
    const class mshMesh &mesh,
    int arg1,
    int arg2,
    bool arg3,
    rmcModelInfo *info,
    bool arg4,
    const char *name,
    bool arg5
) {
    (void)shaderGroup; (void)arg1; (void)arg2; (void)arg3; (void)info; (void)arg4; (void)arg5;
    rmcModel *m = new rmcModel();
    m->SetModel(0, gfxModelFromMesh(mesh));
    return m;
}

// ---------------------------------------------------------------------------
// rmcModelGeom
// ---------------------------------------------------------------------------

rmcModelGeom::rmcModelGeom()
    : rmcModel()
    , m_Geometry(nullptr)
    , m_ColorData(nullptr)
{
}

rmcModelGeom::rmcModelGeom(datResource &rsc)
    : rmcModel(rsc)
    , m_Geometry(nullptr)
    , m_ColorData(nullptr)
{
#if defined(__WIN32PC)
    u32 start = rsc.Tell();
    rsc.PointerFixup(m_Geometry);
    rsc.PointerFixup(m_ColorData);
    u32 consumed = rsc.Tell() - start;
    (void)consumed;
#else
    rsc.PointerFixup(m_Geometry);
    rsc.PointerFixup(m_ColorData);
#endif
}

rmcModelGeom::~rmcModelGeom()
{
}

void rmcModelGeom::Draw(const rmcShaderGroup &shaders, const rmcShaderData *data, int bucket, int lod) const
{
    rmcModel::Draw(shaders, data, bucket, lod);
}

void rmcModelGeom::DrawCpv(const rmcShaderGroup &shaders, const rmcShaderData *data, int bucket, int lod, int cpvIndex) const
{
    rmcModel::DrawCpv(shaders, data, bucket, lod, cpvIndex);
}

void rmcModelGeom::DrawSkinned(const rmcShaderGroup &shaders, const rmcShaderData *data, const Matrix34 *mtxs, int mtxCount, int bucket, int lod, const atBitSet *enables) const
{
    rmcModel::DrawSkinned(shaders, data, mtxs, mtxCount, bucket, lod, enables);
}

bool rmcModelGeom::Load(class mshMesh &mesh, bool instanceCpv, const char *name)
{
    (void)instanceCpv; (void)name;
    m_GfxModel = gfxModelFromMesh(mesh);
    return m_GfxModel != nullptr;
}
