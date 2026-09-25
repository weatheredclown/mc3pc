#include "rmcore/texture.h"
#include "rmcore/texturedefault.h"
#include "gfx/texture.h"
#include "gfx/simple.h"
#include "gfx/vgl.h"
#include <vector>
#include <ctype.h>
#include <string.h>
#include <string>
#include <map>

rmcTexture * rmcTexture::None = nullptr;
rmcTextureFactory * rmcTextureFactory::sm_Instance = nullptr;

struct TexRefCmp {
    bool operator()(const std::string &a, const std::string &b) const {
        return _stricmp(a.c_str(), b.c_str()) < 0;
    }
};

static std::map<std::string, rmcTexture*, TexRefCmp> s_TextureReferences;

void rmcTextureFactory::RegisterTextureReference(const char *name, const rmcTexture *tex) {
    if (!name) return;
    s_TextureReferences[name] = const_cast<rmcTexture*>(tex);
}

void rmcTextureFactory::RegisterTextureReference(const char *name, gfxTexture *tex) {
    if (!name) return;
    if (tex) {
        s_TextureReferences[name] = new rmcTextureGfx(tex);
    } else {
        s_TextureReferences.erase(name);
    }
}

void rmcTextureFactory::RegisterTextureReference(const char *name, const char *path) {
    if (!name) return;
    if (path) {
        gfxTexture *tex = gfxGetTexture(path);
        if (tex) {
            s_TextureReferences[name] = new rmcTextureGfx(tex);
        }
    }
}

void rmcTextureFactory::DeleteTextureReference(const char *name) {
    if (!name) return;
    s_TextureReferences.erase(name);
}

rmcTexture *rmcTextureFactory::LookupTextureReference(const char *name, bool &found) {
    found = false;
    if (!name) return nullptr;
    auto it = s_TextureReferences.find(name);
    if (it != s_TextureReferences.end()) {
        found = true;
        return it->second;
    }
    return nullptr;
}

rmcTexture *rmcTextureFactory::LookupTextureReference(const char *name) {
    bool dummy;
    return LookupTextureReference(name, dummy);
}

rmcTexture *rmcTextureFactory::Create(const char *filename) {
    return rmcTextureFactoryDefault::CreateDefault(filename);
}

void rmcTextureFactory::ResourcePageIn(class datResource &res, rmcTexture **texList, int count) {
    if (!texList || count <= 0) return;
    for (int i = 0; i < count; ++i) {
        res.PointerFixup(texList[i]);
    }
}

// Placeholder textures the game asks for by name but that are on no disc (the
// console built them in).  "*blank*" decal / logo slots become a transparent
// 1x1; the metal-paint specular "blank" is the base image the paint gradient is
// rendered into, so it gets a 64x64 black image like the fx_metalpaintN maps.
// Registered under the requested name so the next lookup hits the registry.
static gfxTexture *sMakePlaceholderTexture(const char *name) {
    char lower[128];
    strncpy(lower, name, sizeof(lower) - 1); lower[sizeof(lower) - 1] = 0;
    for (char *c = lower; *c; c++) *c = (char)tolower(*c);
    if (!strstr(lower, "blank")) return NULL;
    int w = 1, h = 1;
    unsigned char fill[4] = {0, 0, 0, 0};
    if (strstr(lower, "metalpaint") || strstr(lower, "specular")) {
        // the metal-flake specular base: the console painted a gradient into it
        // (mcCarMetallicPaint::SetupImage is PS2-only), so on PC use the disc's
        // first flake map as the default look, else a black (no flake) image
        // (asked for under a pushed vehicle folder: anchor the lookup at the asset root)
        gfxTexture *real = gfxGetTexture("$/texture/fx_metalpaint0", true, true);
        if (!real) real = gfxGetTexture("fx_metalpaint0", true, true);
        if (real) { Displayf("rmcTextureDefault: '%s' is not on disc, using fx_metalpaint0", name); return real; }
        w = h = 64; fill[3] = 255;
    }
    std::vector<unsigned char> rgba((size_t)w * h * 4);
    for (int k = 0; k < w * h; k++) memcpy(&rgba[(size_t)k * 4], fill, 4);
    gfxTexture *tex = gfxRegisterRgbaTexture(name, w, h, &rgba[0]);
    if (tex) Displayf("rmcTextureDefault: '%s' is not on disc, using a %dx%d placeholder (%s)", name, w, h, fill[3] ? "black" : "transparent");
    return tex;
}

gfxTexture *(*rmcTextureDefault::sm_PageTextureLookup)(const char *name) = 0;

rmcTextureDefault::rmcTextureDefault(const char *name) : rmcTextureReference(name) {
    if (name && name[0] != '\0' && _stricmp(name, "none") != 0) {
        bool found = false;
        rmcTexture *ref = rmcTextureFactory::LookupTextureReference(name, found);
        if (found && ref) {
            m_Texture = ref;
        } else {
            gfxTexture *tex = gfxGetTexture(name, true, true);
            if (!tex && sm_PageTextureLookup) tex = sm_PageTextureLookup(name);   // page-file textures (licence plates)
            if (!tex) tex = sMakePlaceholderTexture(name);
            if (!tex) tex = gfxGetTexture(name);          // logs the failure
            if (tex) {
                m_Texture = new rmcTextureGfx(tex);
            }
        }
    }
}

void rmcTextureFactory::CreateGfxTextureFactory() {
    static rmcTextureFactoryDefault factory;
    sm_Instance = &factory;
    if (!rmcTexture::None) {
        rmcTexture::None = new rmcTextureGfx(NoTexture);
        // Every "none" / not-found lookup hands back this one object, and the
        // callers Release what the factory gives them, so it has to be pinned
        // or the first Release destroys the shared instance under everyone.
        rmcTexture::None->MakePermanent();
    }
}

class rmcTextureString : public rmcTexture {
public:
    std::string m_Name;
    rmcTextureString(const char* name) : m_Name(name ? name : "") {}
};

class rmcTextureFactoryString : public rmcTextureFactory {
public:
    virtual rmcTexture* Create(const char *filename) override {
        return new rmcTextureString(filename);
    }
};

void rmcTextureFactory::CreateStringTextureFactory() {
    static rmcTextureFactoryString factory;
    sm_Instance = &factory;
    if (!rmcTexture::None) {
        rmcTexture::None = new rmcTextureString("none");
        rmcTexture::None->MakePermanent();   // shared; see CreateGfxTextureFactory
    }
}

#include "gfx/rstate.h"

void rmcTexture::Bind(int stage) const
{
    RSTATE.SetTexture(stage, GetGfxTexture());
}

// ---------------------------------------------------------------------------
// factory lifetime + instance stack
// ---------------------------------------------------------------------------

static rmcTextureFactory *s_DefaultFactory = nullptr;
static rmcTextureFactory *s_FactoryStack[8];
static int s_FactoryDepth = 0;

void rmcTextureFactory::InitClass() {
    if (!sm_Instance) {
        CreateGfxTextureFactory();
        s_DefaultFactory = sm_Instance;
    }
}

void rmcTextureFactory::ShutdownClass() {
    s_FactoryDepth = 0;
    // the gfx factory is a function-static object (CreateGfxTextureFactory); just unhook it
    if (s_DefaultFactory) {
        if (sm_Instance == s_DefaultFactory) sm_Instance = nullptr;
        s_DefaultFactory = nullptr;
    }
}

void rmcTextureFactory::PushInstance(rmcTextureFactory *inst) {
    if (!inst) return;
    if (s_FactoryDepth < 8) s_FactoryStack[s_FactoryDepth++] = sm_Instance;
    sm_Instance = inst;
}

void rmcTextureFactory::PopInstance() {
    if (s_FactoryDepth > 0) sm_Instance = s_FactoryStack[--s_FactoryDepth];
}

bool rmcSaveTextureNames = true;
float rmcTextureFactory::sm_TextureLod = 0.0f;
rmcTagDatabaseClass rmcTagDatabase;

bool rmcTagDatabaseClass::Load(const char * /*name*/) {
    // no offline tag database on PC (packs are read directly)
    m_Loaded = true;
    return true;
}

void rmcTagDatabaseClass::Kill() {
    m_Loaded = false;
}

rmcTextureReference::rmcTextureReference(datResource &rsc)
    : m_Flags(0), m_Pad(0), m_RefIndex(0), m_Name(nullptr), m_Texture(nullptr)
{
#if defined(__WIN32PC)
    u32 start = rsc.Tell();
    rsc.GetVTable();
    m_Flags = rsc.GetU8();
    m_Pad = rsc.GetU8();
    m_RefIndex = rsc.GetU16();
    rsc.PointerFixup(m_Name);
    rsc.PointerFixup(m_Texture);
    u32 consumed = rsc.Tell() - start;
    (void)consumed;
#else
    rsc.PointerFixup(m_Name);
    rsc.PointerFixup(m_Texture);
#endif
}

rmcDictionaryReference::rmcDictionaryReference(datResource &rsc)
    : rmcTextureReference(rsc), m_DictIndex(0), m_EntryIndex(0)
{
#if defined(__WIN32PC)
    u32 start = rsc.Tell();
    m_DictIndex = rsc.GetU16();
    m_EntryIndex = rsc.GetU16();
    u32 consumed = rsc.Tell() - start;
    (void)consumed;
#endif
}

rmcTexturePS2::rmcTexturePS2() {
    memset(this, 0, sizeof(*this));
}

rmcTexturePS2::rmcTexturePS2(const char *name) {
    memset(this, 0, sizeof(*this));
    m_Name = name;
}

rmcTexturePS2::rmcTexturePS2(datResource &rsc) {
    memset(this, 0, sizeof(*this));
#if defined(__WIN32PC)
    u32 start = rsc.Tell();
    rsc.GetVTable();
    m_Flags = rsc.GetU8();
    m_Pad = rsc.GetU8();
    m_RefIndex = rsc.GetU16();
    rsc.Skip(8);
    m_Tex0 = (u64)rsc.GetU32() | ((u64)rsc.GetU32() << 32);
    m_Tex1 = (u64)rsc.GetU32() | ((u64)rsc.GetU32() << 32);
    rsc.Skip(0x28);
    rsc.PointerFixup(m_Image);
    rsc.PointerFixup(m_Clut);
    m_ImageSize = rsc.GetU32();
    rsc.Skip(0x24);
    rsc.PointerFixup(m_Name);
    m_DownloadSize = rsc.GetU32();
    m_Width = rsc.GetU16();
    m_Height = rsc.GetU16();
    m_MipMapCount = rsc.GetU8();
    m_Clamp = rsc.GetU8();
    m_Filter = rsc.GetU8();
    m_Format = rsc.GetU8();
    m_ClampReg = (u64)rsc.GetU32() | ((u64)rsc.GetU32() << 32);
    m_Miptbp1 = (u64)rsc.GetU32() | ((u64)rsc.GetU32() << 32);
    m_Miptbp2 = (u64)rsc.GetU32() | ((u64)rsc.GetU32() << 32);
    u32 consumed = rsc.Tell() - start;
    (void)consumed;
#else
    rsc.PointerFixup(m_Image);
    rsc.PointerFixup(m_Clut);
    rsc.PointerFixup(m_Name);
#endif
}

rmcTextureProxyPS2::rmcTextureProxyPS2(datResource &rsc)
    : m_Pad(0), m_Ptr0x4(nullptr), m_Ptr0x8(nullptr), m_Ptr0xc(nullptr), m_Ptr0x10(nullptr)
{
#if defined(__WIN32PC)
    u32 start = rsc.Tell();
    m_Pad = rsc.GetU32();
    rsc.PointerFixup(m_Ptr0x4);
    rsc.PointerFixup(m_Ptr0x8);
    rsc.PointerFixup(m_Ptr0xc);
    rsc.PointerFixup(m_Ptr0x10);
    u32 consumed = rsc.Tell() - start;
    (void)consumed;
#else
    rsc.PointerFixup(m_Ptr0x4);
    rsc.PointerFixup(m_Ptr0x8);
    rsc.PointerFixup(m_Ptr0xc);
    rsc.PointerFixup(m_Ptr0x10);
#endif
}

void rmcTextureFactory::LockRenderTarget(const rmcRenderTarget *rt, const rmcRenderTarget *zbuffer, gfxViewport *vp, const Matrix34 &cam, int face) {
    (void)cam; (void)face;
    if (rt && rt->GetGfxTexture()) {
        // A zbuffer means the caller is drawing a scene, not a 2D blit: give the
        // target real depth (gfx/texture.h gfxRTTDepth).
        PIPE.SetRenderTarget(rt->GetGfxTexture(), zbuffer ? zbuffer->GetGfxTexture() : NULL);
    }
    if (vp) {
        PIPE.SetViewport(vp);
    }
}

void rmcTextureFactory::LockRenderTarget(const rmcRenderTarget *rt, const rmcRenderTarget *zbuffer, gfxViewport *vp) {
    if (rt && rt->GetGfxTexture()) {
        PIPE.SetRenderTarget(rt->GetGfxTexture(), zbuffer ? zbuffer->GetGfxTexture() : NULL);
    }
    if (vp) {
        PIPE.SetViewport(vp);
    }
}

void rmcTextureFactory::UnlockRenderTarget(rmcRenderTarget *rt) {
    (void)rt;
    PIPE.SetRenderTarget(nullptr);
}

void rmcRenderTarget::Lock() {
    if (GfxTarget) {
        PIPE.SetRenderTarget(GfxTarget, nullptr);
    }
}

void rmcRenderTarget::Unlock() {
    PIPE.SetRenderTarget(nullptr);
}

