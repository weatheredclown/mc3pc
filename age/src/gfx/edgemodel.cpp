////////////////////////////////////////
// edgemodel.cpp
//
// gfxEdgeModel: the ".em" edge list used for stencil shadow volumes.
//
// File format (ascii):
//   version: 100
//   verts: N / tris: N / edges: N / optimized: 0|1
//   v x y z bone        (bone = skeleton bone index the vertex follows)
//   tri a b c
//   e v1 v2 t1 t2       (edge between two verts, shared by two triangles)
//
// GenerateSilhouette() skins the verts, finds the edges whose two triangles
// face opposite ways with respect to the light, and extrudes them away from
// the light into a closed-enough volume that is rasterised into the stencil
// buffer (the caller sets RSTATE.SetStencilMode(1)).  The four colour
// arguments were the PS2 dest-alpha encoding of face winding; the D3D11 path
// uses two-sided stencil so they are ignored.
////////////////////////////////////////

#include "gfx/model.h"
#include "gfx/vgl.h"
#include "gfx/rstate.h"
#include "gfx/simple.h"
#include "data/assetcfg.h"
#include "data/token.h"
#include "core/stream.h"
#include "core/output.h"
#include "vector/matrix34.h"
#include <string.h>
#include <math.h>
#include <vector>

gfxEdgeModel *gfxEdgeModel::Create(const char *name)
{
    // "kno.em" or "kno"
    char base[256];
    strncpy(base, name, sizeof(base) - 1); base[sizeof(base) - 1] = 0;
    size_t n = strlen(base);
    if (n > 3 && !_stricmp(base + n - 3, ".em")) base[n - 3] = 0;

    Stream *s = ASSET.Open(base, "em");
    if (!s) {
        Warningf("gfxEdgeModel: can't open '%s.em'", base);
        return NULL;
    }
    datTokenizer tok;
    tok.Init(base, s);
    gfxEdgeModel *em = Create(tok);
    s->Close();
    return em;
}

gfxEdgeModel *gfxEdgeModel::Create(datTokenizer &tok)
{
    gfxEdgeModel *em = new gfxEdgeModel();
    char t[64];
    int nVerts = 0, nTris = 0, nEdges = 0;
    // header (fixed order)
    tok.MatchToken("version:"); tok.GetInt();
    tok.MatchToken("verts:");   nVerts = tok.GetInt();
    tok.MatchToken("tris:");    nTris = tok.GetInt();
    tok.MatchToken("edges:");   nEdges = tok.GetInt();
    tok.MatchToken("optimized:"); em->IsOptimized = tok.GetInt() != 0;

    em->Verts.reserve(nVerts); em->Tris.reserve(nTris); em->Edges.reserve(nEdges);
    while (tok.GetToken(t, sizeof(t)) > 0) {
        if (!strcmp(t, "v")) {
            Vert v;
            v.Pos.x = tok.GetFloat(); v.Pos.y = tok.GetFloat(); v.Pos.z = tok.GetFloat();
            v.Bone = tok.GetInt();
            em->Verts.push_back(v);
        } else if (!strcmp(t, "tri")) {
            Tri tr; tr.V[0] = tok.GetInt(); tr.V[1] = tok.GetInt(); tr.V[2] = tok.GetInt();
            em->Tris.push_back(tr);
        } else if (!strcmp(t, "e")) {
            Edge e; e.V[0] = tok.GetInt(); e.V[1] = tok.GetInt(); e.T[0] = tok.GetInt(); e.T[1] = tok.GetInt();
            em->Edges.push_back(e);
        }
    }
    // validate indices so a bad file can't crash the silhouette pass
    int nv = (int)em->Verts.size(), nt = (int)em->Tris.size();
    for (size_t i = 0; i < em->Tris.size(); i++)
        for (int k = 0; k < 3; k++) if (em->Tris[i].V[k] < 0 || em->Tris[i].V[k] >= nv) em->Tris[i].V[k] = 0;
    std::vector<Edge> ok;
    for (size_t i = 0; i < em->Edges.size(); i++) {
        const Edge &e = em->Edges[i];
        if (e.V[0] >= 0 && e.V[0] < nv && e.V[1] >= 0 && e.V[1] < nv && e.T[0] >= 0 && e.T[0] < nt && e.T[1] >= 0 && e.T[1] < nt)
            ok.push_back(e);
    }
    em->Edges.swap(ok);
    return em;
}

bool gfxEdgeModel::Packet::IsCW(const Vector3 &v1, const Vector3 &v2, const Vector3 &v3)
{
    Vector3 a, b, n;
    a.Subtract(v2, v1); b.Subtract(v3, v1); n.Cross(a, b);
    return n.z < 0.0f;
}

void gfxEdgeModel::GenerateSilhouette(const SilSource &src, float extrude, int bones, const Matrix34 *m,
                                      u32 /*cw*/, u32 /*ccw*/, u32 /*cwcap*/, u32 /*ccwcap*/) const
{
    if (Verts.empty() || Tris.empty() || Edges.empty() || !m) return;

    // 1. skin
    std::vector<Vector3> pos(Verts.size());
    for (size_t i = 0; i < Verts.size(); i++) {
        int b = Verts[i].Bone;
        if (b < 0 || b >= bones) b = 0;
        m[b].Transform(Verts[i].Pos, pos[i]);
    }

    // 2. per-vertex extrusion offset and per-triangle facing
    Vector3 dir(src.Pos);                  // asDir: direction the light travels
    if (src.Style == asDir) { if (dir.Mag2() > 1e-8f) dir.Normalize(); else dir.Set(0, -1, 0); }
    std::vector<Vector3> ext(pos.size());
    for (size_t i = 0; i < pos.size(); i++) {
        Vector3 d;
        if (src.Style == asPoint) { d.Subtract(pos[i], src.Pos); if (d.Mag2() > 1e-8f) d.Normalize(); else d.Set(0, -1, 0); }
        else d = dir;
        ext[i].AddScaled(pos[i], d, extrude);
    }
    std::vector<unsigned char> lit(Tris.size());
    for (size_t i = 0; i < Tris.size(); i++) {
        const Tri &t = Tris[i];
        Vector3 a, b, n;
        a.Subtract(pos[t.V[1]], pos[t.V[0]]); b.Subtract(pos[t.V[2]], pos[t.V[0]]); n.Cross(a, b);
        Vector3 toLight;
        if (src.Style == asPoint) toLight.Subtract(src.Pos, pos[t.V[0]]);
        else toLight.Negate(dir);
        lit[i] = n.Dot(toLight) > 0.0f ? 1 : 0;
    }

    // 3. silhouette quads: edge order taken from the lit triangle so every quad
    //    winds outward (front faces seen from outside the volume).
    int quads = 0;
    for (size_t i = 0; i < Edges.size(); i++) quads += (lit[Edges[i].T[0]] != lit[Edges[i].T[1]]) ? 1 : 0;
    if (!quads) return;

    vglBegin(drawTriangles, quads * 6);
    for (size_t i = 0; i < Edges.size(); i++) {
        const Edge &e = Edges[i];
        if (lit[e.T[0]] == lit[e.T[1]]) continue;
        const Tri &lt = Tris[lit[e.T[0]] ? e.T[0] : e.T[1]];
        int a = e.V[0], b = e.V[1];
        // is a->b the winding order in the lit tri?
        bool fwd = false;
        for (int k = 0; k < 3; k++) if (lt.V[k] == a && lt.V[(k + 1) % 3] == b) fwd = true;
        if (!fwd) { int tmp = a; a = b; b = tmp; }
        // quad a, b, b+ext, a+ext  (two tris)
        vglVertex3f(pos[a]);  vglVertex3f(pos[b]);  vglVertex3f(ext[b]);
        vglVertex3f(pos[a]);  vglVertex3f(ext[b]);  vglVertex3f(ext[a]);
    }
    vglEnd();
}

////////////////////////////////////////////////////////////////////////////
// Stencil shadow pass.  The PS2 version encoded face winding in destination
// alpha (the four colour arguments) and resolved it with a blit; here the
// two-sided stencil count of RSTATE.SetStencilMode(1) marks the covered
// pixels and Finalize darkens them once with a full-window quad drawn where
// the count is non-zero (SetStencilMode(2)).

static bool sShadowPassActive = false;
static bool sShadowSavedZWrite = true, sShadowSavedZTest = true, sShadowSavedBlend = false, sShadowSavedFog = false;
static gfxCullMode sShadowSavedCull = cullNone;
static EnumBlendSet sShadowSavedBlendSet = blendSet_One_Zero;

void gfxEdgeModel::InitShadowVolumePass()
{
    if (sShadowPassActive) return;
    sShadowPassActive = true;
    sShadowSavedZWrite = RSTATE.GetZWriteEnable();
    sShadowSavedZTest = RSTATE.GetZTestEnable();
    sShadowSavedBlend = RSTATE.GetAlphaBlendEnable();
    sShadowSavedFog = RSTATE.GetFogEnable();
    sShadowSavedCull = RSTATE.GetCull();
    sShadowSavedBlendSet = RSTATE.GetBlendSet();
    PIPE.Clear(gfxPipeline::clearStencil);
    RSTATE.SetTexture(NULL);
    RSTATE.SetFogEnable(false);
    RSTATE.SetAlphaBlendEnable(false);
    RSTATE.SetZTestEnable(true);
    RSTATE.SetZWriteEnable(false);
    RSTATE.SetCull(cullNone);          // both faces count (front +1, back -1)
    RSTATE.SetStencilMode(1);
}

bool gfxEdgeModel::IsShadowVolumePassActive() { return sShadowPassActive; }

void gfxEdgeModel::FinalizeShadowVolumePass(u32 color)
{
    if (!sShadowPassActive) return;
    // Darken where the count is non-zero: dest - color.  The PS2 volume
    // pass subtracted its colour once per face crossing (front and back of
    // a convex volume), so double it for the same overall darkening.
    unsigned r = ((color >> 16) & 255) * 2, g = ((color >> 8) & 255) * 2, b = (color & 255) * 2;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    RSTATE.SetStencilMode(2);
    RSTATE.SetTexture(NULL);
    RSTATE.SetAlphaBlendEnable(true);
    RSTATE.SetBlendSet(blendSet_MinusOne_One);
    RSTATE.SetZTestEnable(false);
    RSTATE.SetZWriteEnable(false);
    const gfxViewportParams &p = PIPE.GetViewport()->GetViewportParams();
    PIPE.Blit2D(p.m_X, p.m_Y, p.m_X + p.m_Width, p.m_Y + p.m_Height, 0.0f, 0.0f, 1.0f, 1.0f, mkrgba((u8)r, (u8)g, (u8)b, 255));
    RSTATE.SetStencilMode(0);
    RSTATE.SetBlendSet(sShadowSavedBlendSet);
    RSTATE.SetAlphaBlendEnable(sShadowSavedBlend);
    RSTATE.SetZTestEnable(sShadowSavedZTest);
    RSTATE.SetZWriteEnable(sShadowSavedZWrite);
    RSTATE.SetFogEnable(sShadowSavedFog);
    RSTATE.SetCull(sShadowSavedCull);
    sShadowPassActive = false;
}

void gfxEdgeModel::GenerateShadowVolume(const SilSource &src, float f, int bones, const Matrix34 *m,
                                        u32 c1, u32 c2, u32 c3, u32 c4, bool /*closed*/, bool /*zpass*/) const
{
    if (sShadowPassActive) {
        GenerateSilhouette(src, f, bones, m, c1, c2, c3, c4);
        return;
    }
    // Stand-alone: a whole pass for this volume, darkened by c1.
    InitShadowVolumePass();
    GenerateSilhouette(src, f, bones, m, c1, c2, c3, c4);
    FinalizeShadowVolumePass(c1);
}

/* End of file gfx/edgemodel.cpp */
