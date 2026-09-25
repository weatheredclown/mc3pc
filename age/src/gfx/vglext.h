#ifndef GFX_VGLEXT_H
#define GFX_VGLEXT_H

#include "core/output.h"
#include "vector/Matrix34.h"
#include "vector/vector2.h"
#include "gfx/vgl.h"
#include <math.h>

// VIF command macros
#define RAW_VIF_CMD_NOP 0
#define RAW_VIF_CMD_UNPACK_V2_32 0
#define RAW_VIF_CMD_UNPACK_V3_32 0
#define RAW_VIF_CMD_UNPACK_V4_32 0
#define RAW_VIF_CMD_UNPACK_UNSIGNED 0
#define RAW_VIF_CMD_UNPACK_FLG_OFF 0
#define RAW_VIF_CMD_MSCAL 0
#define RAW_VIF_CMD_MSCAL_ADRD8(x) 0
#define RAW_VIF_NUM(x) 0
#define RAW_VIF_CMD_UNPACK_ADDR(x) 0

// VIF1 / GIF macros
#define RAW_VIF1_CMD_DIRECT 0
#define RAW_VIF1_CMD_DIRECT_NUM(x) 0
#define RAW0_GIF_NLOOP(x) 0
#define RAW0_GIF_EOP_ON 0
#define RAW1_GIF_PRE_OFF 0
#define RAW1_GIF_FLG_PACKED 0
#define RAW1_GIF_NREG(x) 0
#define RAW_GIF_REG_AND 0

// GS Registers
#define SCE_GS_PRMODE 0
#define SCE_GS_ALPHA_1 0
#define SCE_GS_TEST_1 0
#define SCE_GS_ZBUF_1 0
#define SCE_GS_TEXA 0

extern const int MainMicrocode;

// PS2 VU1 microcode / DMA chain builder.  Every caller in the game sits
// behind #if __PSX2; the PC build keeps the type so those headers parse.
// There is no DMA on this backend: AddDMAInline hands back scratch the
// caller may write into and nothing consumes it.
class lowPsxGfx {
public:
    static void DoMicrocode(const void* microcode) { (void)microcode; }

    void* AddDMAInline(int size, int arg2, int arg3) {
        (void)size; (void)arg2; (void)arg3;
        static char dummyBuffer[8192];
        return dummyBuffer;
    }

    void AddDMARef(void* ptr, int size, int arg3, int arg4) { (void)ptr; (void)size; (void)arg3; (void)arg4; }
};

extern lowPsxGfx LOWPSXGFX;

// Vertex format selection (eFVF in gfx/model.h).  The D3D11 backend carries
// position, colour, two UV sets and a normal in every vertex, so this only
// records the format for GetFormat readers.
void vglSetFormat(int format);
int vglGetFormat();
// Batch brackets: the PS2 opened/closed a DMA batch here.  On PC every
// vglEnd submits, so the brackets only reset per-batch texgen state.
inline void vglBeginBatch() { vglResetTexGen(); }
inline void vglEndBatch() { Quitf("vglEndBatch - not implemented"); }
inline void vglDrawAxis(float scale, const Matrix34 &m) {
    Vector3 origin(0.0f, 0.0f, 0.0f);
    Vector3 x(scale, 0.0f, 0.0f);
    Vector3 y(0.0f, scale, 0.0f);
    Vector3 z(0.0f, 0.0f, scale);

    m.Transform(origin, origin);
    m.Transform(x, x);
    m.Transform(y, y);
    m.Transform(z, z);

    vglBegin(drawLine, 6);
    
    // X (Red)
    vglColor(mkfrgb(1.0f, 0.0f, 0.0f));
    vglVertex3f(origin);
    vglVertex3f(x);

    // Y (Green)
    vglColor(mkfrgb(0.0f, 1.0f, 0.0f));
    vglVertex3f(origin);
    vglVertex3f(y);

    // Z (Blue)
    vglColor(mkfrgb(0.0f, 0.0f, 1.0f));
    vglVertex3f(origin);
    vglVertex3f(z);

    vglEnd();
}

inline void vglVertex2f(const Vector2 &v) { vglVertex3f(v.x, v.y, 0.0f); }
inline void vglVertex2f(const Vector3 &v) { vglVertex3f(v.x, v.y, 0.0f); }
inline void vglVertex2f(float x, float y) { vglVertex3f(x, y, 0.0f); }
// Camera-facing textured quad of `size` world units centred on pos, drawn
// with the current texture and blend state (rain splashes, sparks).  (rgl.cpp)
void vglDrawParticle(const Vector3 &pos, float size, const Vector4 &color);
// Immediate-mode 2D: vglOrtho(true) makes vgl vertices screen-space pixels
// (HUD, menus, blits) until vglOrtho(false); the backend keys its projection
// off the same flag pipeManager::SetViewport uses for the ortho viewport.
// vglSetScreenOrtho marks the explicit request, so the pixels still address
// the screen inside a viewport with its own Ortho2D range (the HUD map).
void vglSetViewportOrtho(bool ortho);
void vglSetScreenOrtho(bool screen);
inline void vglOrtho(bool enable) { vglSetViewportOrtho(enable); vglSetScreenOrtho(enable); }
// Debug primitives (bank toggles: camera/AI/grab/pad visualisers), drawn as
// lines with the current vgl colour.
inline void vglDrawSphere(float radius, const Matrix34 &mat, int segments = 16, bool wireframe = true) {
    if (segments < 4) segments = 4;
    for (int axis = 0; axis < 3; axis++) {
        vglBegin(drawLineStrip, segments + 1);
        for (int i = 0; i <= segments; i++) {
            float a = 6.2831853f * (float)i / (float)segments;
            float c = cosf(a) * radius, sn = sinf(a) * radius;
            Vector3 p = axis == 0 ? Vector3(0.0f, c, sn) : axis == 1 ? Vector3(c, 0.0f, sn) : Vector3(c, sn, 0.0f);
            Vector3 w; mat.Transform(p, w);
            vglVertex3f(w);
        }
        vglEnd();
    }
}
inline void vglDrawSphere(float radius, const class Vector3 &pos, int segments = 16, bool wireframe = true) {
    Matrix34 m; m.Identity(); m.d = pos;
    vglDrawSphere(radius, m, segments, wireframe);
}
inline void vglDrawSphere(float radius, int segments) {
    Matrix34 m; m.Identity();
    vglDrawSphere(radius, m, segments, true);
}
inline void vglDraw2dCircle(float radius, const class Vector2 &pos, int segments = 16, bool wireframe = true) {
    if (segments < 4) segments = 4;
    vglBegin(drawLineStrip, segments + 1);
    for (int i = 0; i <= segments; i++) {
        float a = 6.2831853f * (float)i / (float)segments;
        vglVertex3f(pos.x + cosf(a) * radius, pos.y + sinf(a) * radius, 0.0f);
    }
    vglEnd();
}
// Vertical (Y-up) cylinder of the given height centred on pos: two rings of
// `segments` points joined by verticals (wireframe) or a closed side wall
// plus caps (solid).
inline void vglDrawCylinder(float height, float radius, const class Vector3 &pos, int segments = 16, bool wireframe = true) {
    if (segments < 3) segments = 3;
    const float y0 = pos.y - height * 0.5f, y1 = pos.y + height * 0.5f;
    const float step = 6.2831853f / (float)segments;
    if (wireframe) {
        for (int ring = 0; ring < 2; ring++) {
            float y = ring ? y1 : y0;
            vglBegin(drawLineStrip, segments + 1);
            for (int i = 0; i <= segments; i++) {
                float a = step * (float)(i % segments);
                vglVertex3f(pos.x + radius * cosf(a), y, pos.z + radius * sinf(a));
            }
            vglEnd();
        }
        vglBegin(drawLine, segments * 2);
        for (int i = 0; i < segments; i++) {
            float a = step * (float)i;
            vglVertex3f(pos.x + radius * cosf(a), y0, pos.z + radius * sinf(a));
            vglVertex3f(pos.x + radius * cosf(a), y1, pos.z + radius * sinf(a));
        }
        vglEnd();
    } else {
        vglBegin(drawTriStrip, (segments + 1) * 2);
        for (int i = 0; i <= segments; i++) {
            float a = step * (float)(i % segments);
            float x = pos.x + radius * cosf(a), z = pos.z + radius * sinf(a);
            vglVertex3f(x, y1, z);
            vglVertex3f(x, y0, z);
        }
        vglEnd();
        for (int ring = 0; ring < 2; ring++) {
            float y = ring ? y1 : y0;
            vglBegin(drawTriFan, segments + 2);
            vglVertex3f(pos.x, y, pos.z);
            for (int i = 0; i <= segments; i++) {
                // wind caps so both face outward
                float a = step * (float)((ring ? i : segments - i) % segments);
                vglVertex3f(pos.x + radius * cosf(a), y, pos.z + radius * sinf(a));
            }
            vglEnd();
        }
    }
}

inline void vglDrawEllipticCylinder(const Vector3 &box, const Matrix34 &mtx, int segments = 16, bool wireframe = true) {
    if (segments < 4) segments = 4;
    float rx = box.x;
    float rz = box.z;
    float halfH = box.y * 0.5f;
    float step = 6.2831853f / (float)segments;
    vglBegin(drawLineStrip, segments + 1);
    for (int i = 0; i <= segments; i++) {
        float a = step * (float)(i % segments);
        Vector3 p(cosf(a) * rx, halfH, sinf(a) * rz);
        Vector3 w; mtx.Transform(p, w);
        vglVertex3f(w);
    }
    vglEnd();
    vglBegin(drawLineStrip, segments + 1);
    for (int i = 0; i <= segments; i++) {
        float a = step * (float)(i % segments);
        Vector3 p(cosf(a) * rx, -halfH, sinf(a) * rz);
        Vector3 w; mtx.Transform(p, w);
        vglVertex3f(w);
    }
    vglEnd();
    vglBegin(drawLine, segments * 2);
    for (int i = 0; i < segments; i++) {
        float a = step * (float)i;
        Vector3 p1(cosf(a) * rx, halfH, sinf(a) * rz);
        Vector3 p2(cosf(a) * rx, -halfH, sinf(a) * rz);
        Vector3 w1, w2;
        mtx.Transform(p1, w1);
        mtx.Transform(p2, w2);
        vglVertex3f(w1);
        vglVertex3f(w2);
    }
    vglEnd();
}

inline void vglDrawBox(const Vector3 &size, const Matrix34 &mtx);
inline void vglDrawBox(const Vector3 &size, const Matrix34 &mtx, gfxPackedColor color) {
    vglColor(color);
    vglDrawBox(size, mtx);
}
inline void vglDrawBox(const Vector3 &size, const Vector3 &pos, gfxPackedColor color) {
    Matrix34 m; m.Identity(); m.d = pos;
    vglColor(color);
    vglDrawBox(size, m);
}
inline void vglDrawBox(const Vector3 &size, const Matrix34 &mtx) {
    Vector3 h(size.x * 0.5f, size.y * 0.5f, size.z * 0.5f);
    Vector3 c[8];
    for (int i = 0; i < 8; i++) {
        Vector3 p((i & 1) ? h.x : -h.x, (i & 2) ? h.y : -h.y, (i & 4) ? h.z : -h.z);
        mtx.Transform(p, c[i]);
    }
    static const int e[12][2] = {{0,1},{1,3},{3,2},{2,0},{4,5},{5,7},{7,6},{6,4},{0,4},{1,5},{2,6},{3,7}};
    vglBegin(drawLine, 24);
    for (int i = 0; i < 12; i++) { vglVertex3f(c[e[i][0]]); vglVertex3f(c[e[i][1]]); }
    vglEnd();
}
inline void vglDrawHotdog(float length, float radius, const Matrix34 &mat, int segments) {
    // capsule along the matrix's c axis, centred on d
    Matrix34 a(mat), b(mat);
    a.d.AddScaled(mat.c, -length * 0.5f);
    b.d.AddScaled(mat.c,  length * 0.5f);
    vglDrawSphere(radius, a, segments, true);
    vglDrawSphere(radius, b, segments, true);
    vglBegin(drawLine, 8);
    for (int i = 0; i < 4; i++) {
        Vector3 off = (i == 0) ? Vector3(radius, 0, 0) : (i == 1) ? Vector3(-radius, 0, 0) : (i == 2) ? Vector3(0, radius, 0) : Vector3(0, -radius, 0);
        Vector3 p0, p1; a.Transform3x3(off, p0); p0.Add(a.d); b.Transform3x3(off, p1); p1.Add(b.d);
        vglVertex3f(p0); vglVertex3f(p1);
    }
    vglEnd();
}
inline void vglDrawSolidBox(const class Vector3 &size, const class Matrix34 &mtx, const class Vector3 &color = Vector3(1,1,1)) {
    Vector3 h(size.x * 0.5f, size.y * 0.5f, size.z * 0.5f);
    Vector3 c[8];
    for (int i = 0; i < 8; i++) {
        Vector3 p((i & 1) ? h.x : -h.x, (i & 2) ? h.y : -h.y, (i & 4) ? h.z : -h.z);
        mtx.Transform(p, c[i]);
    }
    static const int f[6][4] = {{0,2,3,1},{4,5,7,6},{0,1,5,4},{2,6,7,3},{0,4,6,2},{1,3,7,5}};
    vglColor3f(color);
    vglBegin(drawQuads, 24);
    for (int i = 0; i < 6; i++) for (int k = 0; k < 4; k++) vglVertex3f(c[f[i][k]]);
    vglEnd();
}

// PS2 full-screen fog pass: the GS kept per-pixel fog in destination alpha
// and a final sprite lerped the frame toward the fog colour by that alpha.
// gfxFogSprite reproduces the sprite (colour * DA + frame * (1 - DA)) over
// the current viewport window.  (rgl.cpp)
void gfxFogSprite(gfxPackedColor color);
// gfxFogTexSprite fogged through a CLUT lookup of the alpha channel and
// gfxMoveRG2BA copied the frame's R/G channels into B/A via a GS-local blit
// to stage that alpha; neither has a framebuffer-format analogue on D3D11,
// where fog is applied per pixel in the shader (RSTATE.SetFogParams), so the
// PC frame is already fogged by the time the game calls them.
inline void gfxFogTexSprite(int clut_addr) { (void)clut_addr; }
inline void gfxMoveRG2BA(u32 zbp, u32 fbp) { (void)zbp; (void)fbp; }

#endif // GFX_VGLEXT_H
