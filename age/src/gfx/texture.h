#ifndef GFX_TEXTURE_H
#define GFX_TEXTURE_H

#include <string>
#include <vector>

struct ID3D11Texture2D;
struct ID3D11ShaderResourceView;
struct ID3D11RenderTargetView;
class gfxTexture;
extern "C" void gfxEnvProbeUpdate(gfxTexture *tex);

class gfxTexture {
    friend gfxTexture *gfxGetTexture(const char *name, bool mipmaps, bool silent);
    friend class gfxModel;
    friend void FlushLines();
    friend int Main();
    friend gfxTexture *gfxCreateTextTarget(int w, int h);
    friend void gfxBeginRenderToTexture(gfxTexture *tex);
    friend void gfxBeginRenderToTexture(gfxTexture *tex, int flags, unsigned clearColor);
    friend gfxTexture *gfxGetBackBufferCopy();
    friend bool gfxCopyTexture(gfxTexture *dst, const gfxTexture *src);
    friend void gfxAliasTexture(gfxTexture *dst, const gfxTexture *src);
    friend class gfxBitmap;
    friend gfxTexture *gfxRegisterRgbaTexture(const char *name, int width, int height, const unsigned char *rgba);
    friend bool gfxUpdateRgbaTexture(gfxTexture *tex, int width, int height, const unsigned char *rgba);
    friend void gfxSaveTexture(gfxTexture *tex, const char *path);
    friend bool gfxReadTexturePixels(gfxTexture *tex, std::vector<unsigned char> &rgba, int &width, int &height);
    friend void gfxEnvProbeUpdate(gfxTexture *tex);

private:
    std::string Name;
    int Width;
    int Height;
    struct ID3D11Texture2D *D3DTexture;
    struct ID3D11ShaderResourceView *SRV;
    struct ID3D11RenderTargetView *RTV;   // offscreen text targets only
    int m_TexEnv;
    int RefCount;
    bool m_HasAlpha;
    bool m_AllTranslucent;
    std::vector<unsigned char> m_Indices;   // 8-bit paletted sources keep their index map
    class gfxTexturePalette *m_Palette;     // ... and CLUT, so effects can recolour them
    int m_Download = 0;

public:
    void *m_User;
    gfxTexture();
    gfxTexture(class datResource &rsc);   // resource-image texture (flash movie atlases)
    virtual ~gfxTexture();

    const char* GetName() const { return Name.c_str(); }
    void SetName(const char *name) { Name = name; }
    int GetTexEnv() const { return m_TexEnv; }
    bool HasAlpha() const { return m_HasAlpha; }
    void SetHasAlpha(bool alpha) { m_HasAlpha = alpha; }
    bool IsAllTranslucent() const { return m_AllTranslucent; }
    void SetAllTranslucent(bool trans) { m_AllTranslucent = trans; }
    // Every successful gfxGetTexture() hands the caller one reference; the
    // name cache itself is non-owning, so the last Release() frees the D3D
    // resources and unregisters the name.
    void AddRef() { ++RefCount; }
    void Release() { if (--RefCount <= 0) delete this; }
    int GetRefCount() const { return RefCount; }
    // PS2 texture-download priority (which VRAM upload slot a texture took).
    // Textures are always resident on PC; the value round-trips for readers.
    void SetDownload(int slot) { m_Download = slot; }
    int GetDownload() const { return m_Download; }
    int GetWidth() const { return Width; }
    int GetHeight() const { return Height; }

    enum {
        rtarget24Bits = 1,
        rtargetTexMem = 2,
        rtargetUseFBW = 4,
        rtargetFrontFBMem = 8,
        rtargetBackFBMem = 16,
        rtarget16Bits = 32,
        rtarget32Bits = 64,
        rtargetTexBaseMem = 128,
        rtargetZBuff = 256
    };

    static gfxTexture *CreateRenderTarget(int w, int h) {
        return CreateRenderTarget(w, h, 0);
    }

    static gfxTexture *CreateRenderTarget(int w, int h, int flags = 0, int offset = 0);
    // Upload an RGBA8 image as a texture (the image stays with the caller).
    static gfxTexture *Create(class gfxImage *image, bool mipmaps = false);

    void SetTexEnv(int env) { m_TexEnv = env; }
    class gfxTexturePalette* GetPalette() const { return m_Palette; }
    void UpdateFromPalette();   // re-expand m_Indices through m_Palette and re-upload
};

inline int gfxSetTextureLoadPhase(int phase = 0) {
    static int s_phase = 0;
    int old = s_phase;
    s_phase = phase;
    return old;
}

inline int gfxSetTextureRenderPhase(int phase = 0) {
    static int s_phase = 0;
    int old = s_phase;
    s_phase = phase;
    return old;
}

inline int gfxGetTextureRenderPhase() {
    int cur = gfxSetTextureRenderPhase(0);
    gfxSetTextureRenderPhase(cur);
    return cur;
}

enum {
    gfxTexEnvAlpha = 1,
    gfxTexEnvRGBAOnly = 2,
    gfxTexEnvBilinear = 4,
    gfxTexEnvClampU = 8,
    gfxTexEnvClampV = 16
};

gfxTexture *gfxGetTexture(const char *name, bool mipmaps = true, bool silent = false);

// Folder searched for textures after the bare name ("texture"; mc3 PC builds switch to "texture_x").
extern const char *gfxLoadImageFolder;
void gfxFreeTexture(gfxTexture *tex);

// Offscreen render-to-texture (rgl.cpp) -- used by RenderStringIntoTexture to
// bake UI text into a texture.  Between Begin/End the immediate-mode 2D path
// (Blit2D and friends) renders into the target with an ortho space of the
// target's dimensions.
gfxTexture *gfxCreateTextTarget(int w, int h);
void gfxBeginRenderToTexture(gfxTexture *tex);
// Flags for the 3D form below.
enum {
    // Bind a depth-stencil buffer (cached per target size) and clear it.  Anything
    // that draws a 3D scene into a texture -- the city environment map, the
    // reflected-objects composite -- needs this; without it the target has no z and
    // the scene composites in draw order.  The 2D users must NOT pass it: they
    // stack overlapping quads at one z, which a depth test would reject.
    gfxRTTDepth = 1,
    // Leave the target's colour alone (compositing several passes into one target).
    gfxRTTNoClear = 2,
};
void gfxBeginRenderToTexture(gfxTexture *tex, int flags, unsigned clearColor);
void gfxEndRenderToTexture();

// Environment probe (gfx/rgl.cpp): a 16x16 CPU-side average of a render target,
// refreshed without stalling (double-buffered staging + DO_NOT_WAIT).  The car
// shading is per-vertex on the CPU and cannot sample a texture, so this is how it
// reads the live city environment map.  Call Update once a frame with the map;
// Sample takes sphere-map UVs (the same mapping the reflection texgen uses) and
// writes three floats; Invalidate drops it when the map goes away.
extern "C" void gfxEnvProbeUpdate(gfxTexture *tex);
extern "C" bool gfxEnvProbeSample(float u, float v, float *rgb);
extern "C" bool gfxEnvProbeValid();
extern "C" void gfxEnvProbeInvalidate();
// The live map itself, for the passes that can sample a texture (chrome reflects
// it through the reflection-vector texgen).  NULL when there is none.
extern "C" class gfxTexture *gfxGetCityEnvMap();
// The probe as a small smooth texture - what the car reflections sample.  The
// raw map has a hard sky/ground seam that comes out as faceted grey patches on
// bodywork; a paint reflection is broad and soft, so it reflects this instead.
extern "C" class gfxTexture *gfxGetCityEnvProbeTexture();
// The living copy of the backbuffer (refreshed before each copy-to-front
// callback); CreateRenderTarget(rtargetBackFBMem) hands out references to it.
gfxTexture *gfxGetBackBufferCopy();
// Re-copy the swapchain into the backbuffer copy (front-buffer reads).
bool gfxRefreshBackBufferCopy();
// GPU copy between two same-sized textures (render targets, the backbuffer
// copy).  Marker textures with no GPU resource return false.
bool gfxCopyTexture(gfxTexture *dst, const gfxTexture *src);
// Make `dst` a second handle on `src`'s GPU resources (same size, shared
// texture/views, each side holding its own reference).
void gfxAliasTexture(gfxTexture *dst, const gfxTexture *src);
bool gfxTextureResident(const char *name);
void gfxAssociateTexture(gfxTexture *tex, const char *name);
gfxTexture *gfxRegisterRgbaTexture(const char *name, int width, int height, const unsigned char *rgba);
// Publish a texture the caller owns under a name, for the binders that work by
// name (the live city environment map is a render target, not a file).  A null
// texture unregisters the name, which the owner must do before it frees it.
bool gfxRegisterExistingTexture(const char *name, class gfxTexture *tex);
// Replace the pixels of a texture that gfxRegisterRgbaTexture made, in place.
// Registration is keyed by name and hands back the existing object for a name
// already in the cache, so anything the game rebuilds while it runs - the
// composited licence plate - has to be updated rather than registered again.
// Size must match; false when it does not, or when there is no device.
bool gfxUpdateRgbaTexture(gfxTexture *tex, int width, int height, const unsigned char *rgba);
inline void gfxGetTextureMovie(gfxTexture* &tex, const char *name, bool arg1 = true, bool arg2 = false) {
    tex = gfxGetTexture(name, arg1, arg2);
}

// Dev texture browser: Draw tiles the loaded textures (names + sizes) over
// the current viewport.  Load names a layout so the tool can title itself
// and pushes the texture folder the layout's textures live in.
class gfxTextureTool {
public:
    gfxTextureTool() : m_Page(0) { m_Layout[0] = 0; m_Path[0] = 0; }
    void Load(const char* layoutName, const char* path);
    void Draw();
    void NextPage() { m_Page++; }
    void PrevPage() { if (m_Page > 0) m_Page--; }
private:
    char m_Layout[64];
    char m_Path[256];
    int m_Page;
};

// Write a texture out as a PNG (debug: see what actually got decoded).
void gfxSaveTexture(class gfxTexture *tex, const char *path);
// Read a texture back off the GPU as tightly packed RGBA8.  False when the
// texture has no D3D resource or the readback fails.
bool gfxReadTexturePixels(class gfxTexture *tex, std::vector<unsigned char> &rgba, int &width, int &height);
// Write a texture as an AGE ".tex" under the asset root, named so that
// gfxGetTexture(name) finds it again.  Used by the asset export: a model is no
// use on its own if the texture its material names does not ship as a file.
bool gfxSaveTextureAsset(class gfxTexture *tex, const char *name);


extern gfxTexture* NoTexture;
// Name cache maintenance: Print lists every cached texture (name, size,
// refs); Prune forgets the failed loads (the cache is non-owning, so a live
// texture leaves with its last Release; only the negative entries linger and
// would block a retry after the texture folder changes); Kill drops every
// cached name so a fresh load never aliases a stale texture.
void gfxTexturePrintHashtable();
void gfxTexturePruneHashtable();
void gfxTextureKillHashtable();

#include "gfx/bitmap.h"

#endif // GFX_TEXTURE_H
