#ifndef RMCORE_TEXTURE_H
#define RMCORE_TEXTURE_H

#include "core/output.h"
#include "gfx/texture.h"
#include "data/resource.h"

class rmcTexture {
public:
    static rmcTexture *None;

    rmcTexture() {}
    virtual ~rmcTexture() {}

    // Reference counting.  A texture handed out by the factory starts at one
    // reference; Release drops one and destroys the wrapper (and with it the
    // gfxTexture, see ~rmcTextureGfx) when the last one goes.  MakePermanent
    // pins a texture the game never wants freed - the shared shader-group
    // textures are pinned this way and then Released freely by their users.
    virtual void AddRef() { ++m_RefCount; }
    virtual void Release() {
        if (m_Permanent) return;
        if (--m_RefCount <= 0) delete this;
    }
    virtual bool Touch() { return true; }
    virtual void MakePermanent() { m_Permanent = true; }
    virtual int GetRefCount() const { return m_RefCount; }
    virtual gfxTexture *GetGfxTexture() const { Quitf("rmcTexture::GetGfxTexture - not implemented"); return nullptr; }
    virtual void FlushTexture() { Quitf("rmcTexture::FlushTexture - not implemented"); }
    virtual int GetWidth() const { gfxTexture *t = GetGfxTexture(); return t ? t->GetWidth() : 0; }
    virtual int GetHeight() const { gfxTexture *t = GetGfxTexture(); return t ? t->GetHeight() : 0; }
    // Bind as the texture for `stage` (RSTATE).
    virtual void Bind(int stage = 0) const;

protected:
    int m_RefCount = 1;
    bool m_Permanent = false;
};

class rmcTextureGfx : public rmcTexture {
public:
    gfxTexture* m_Texture;
    rmcTextureGfx(gfxTexture* tex) : m_Texture(tex) {}
    virtual ~rmcTextureGfx() {
        if (m_Texture && m_Texture != NoTexture) {
            gfxFreeTexture(m_Texture);
        }
    }
    virtual gfxTexture *GetGfxTexture() const override { return m_Texture; }
};

enum rmcRenderTargetType {
    rmcrtBackBuffer = 1,
    rmcrtDepthBuffer = 2
};

class rmcRenderTarget : public rmcTexture {
public:
    int Width;
    int Height;
    gfxTexture *GfxTarget;
    rmcRenderTarget() : Width(256), Height(256), GfxTarget(nullptr) {
        GfxTarget = gfxTexture::CreateRenderTarget(Width, Height, gfxTexture::rtargetTexMem | gfxTexture::rtarget32Bits);
    }
    rmcRenderTarget(int w, int h) : Width(w), Height(h), GfxTarget(nullptr) {
        GfxTarget = gfxTexture::CreateRenderTarget(w, h, gfxTexture::rtargetTexMem | gfxTexture::rtarget32Bits);
    }
    virtual ~rmcRenderTarget() {
        if (GfxTarget) {
            GfxTarget->Release();
            GfxTarget = nullptr;
        }
    }
    virtual int GetWidth() const override { return Width; }
    virtual int GetHeight() const override { return Height; }
    virtual gfxTexture *GetGfxTexture() const override { return GfxTarget; }
    void Lock();
    void Unlock();
};

class rmcTextureReference : public rmcTexture {
public:
    rmcTextureReference() : m_Flags(0), m_Pad(0), m_RefIndex(0), m_Name(nullptr), m_Texture(nullptr) {}
    rmcTextureReference(rmcTexture *tex) : m_Flags(0), m_Pad(0), m_RefIndex(0), m_Name(nullptr), m_Texture(tex) {}
    rmcTextureReference(const char *name, rmcTexture *tex = nullptr) : m_Flags(0), m_Pad(0), m_RefIndex(0), m_Name(name), m_Texture(tex) {}
    rmcTextureReference(const char *name, gfxTexture *tex) : m_Flags(0), m_Pad(0), m_RefIndex(0), m_Name(name), m_Texture(tex ? new rmcTextureGfx(tex) : nullptr) {}
    rmcTextureReference(const char *name, int dummy) : m_Flags(0), m_Pad(0), m_RefIndex(0), m_Name(name), m_Texture(nullptr) { (void)dummy; }
    rmcTextureReference(const char *name, const char *path) : m_Flags(0), m_Pad(0), m_RefIndex(0), m_Name(name), m_Texture(nullptr) { (void)path; }
    rmcTextureReference(const char *name, rmcTexture *tex, u8 flags) : m_Flags(flags), m_Pad(0), m_RefIndex(0), m_Name(name), m_Texture(tex) {}
    rmcTextureReference(datResource &rsc);
    virtual ~rmcTextureReference() {}

    virtual gfxTexture *GetGfxTexture() const override { return m_Texture ? m_Texture->GetGfxTexture() : nullptr; }

    const char *GetName() const { return m_Name; }
    rmcTexture *GetTexture() const { return m_Texture; }
    rmcTexture *GetReference() { return m_Texture ? m_Texture : this; }
    void SetTexture(rmcTexture *tex) { m_Texture = tex; }
    void FlushTexture() { if (m_Texture) m_Texture->FlushTexture(); }
    static void FlushTexture(const char *name) { Quitf("rmcTextureReference::FlushTexture - not implemented"); }
    static void FlushTexture(rmcTexture *tex) { if (tex) tex->FlushTexture(); }

protected:
    u8 m_Flags;            // +0x4
    u8 m_Pad;              // +0x5
    u16 m_RefIndex;        // +0x6
    const char *m_Name;    // +0x8
    rmcTexture *m_Texture; // +0xc
};

class rmcDictionaryReference : public rmcTextureReference {
public:
    rmcDictionaryReference() : m_DictIndex(0), m_EntryIndex(0) {}
    rmcDictionaryReference(const char *name, rmcTexture *tex = nullptr)
        : rmcTextureReference(name, tex), m_DictIndex(0), m_EntryIndex(0) {}
    rmcDictionaryReference(datResource &rsc);
    virtual ~rmcDictionaryReference() {}

    u16 GetDictIndex() const { return m_DictIndex; }
    u16 GetEntryIndex() const { return m_EntryIndex; }

protected:
    u16 m_DictIndex;  // +0x10
    u16 m_EntryIndex; // +0x12
};

class rmcTexturePS2 : public rmcTexture {
public:
    rmcTexturePS2();
    rmcTexturePS2(const char *name);
    rmcTexturePS2(datResource &rsc);
    virtual ~rmcTexturePS2() {}

    const char *GetName() const { return m_Name; }
    virtual int GetWidth() const override { return m_Width; }
    virtual int GetHeight() const override { return m_Height; }

protected:
    u8 m_Flags;              // +0x4
    u8 m_Pad;                // +0x5
    u16 m_RefIndex;          // +0x6
    u64 m_Pad0x8;            // +0x8
    u64 m_Tex0;              // +0x10
    u64 m_Tex1;              // +0x18
    u8 m_Pad0x20[0x28];      // +0x20
    void *m_Image;           // +0x48
    void *m_Clut;            // +0x4c
    u32 m_ImageSize;         // +0x50
    u8 m_Pad0x54[0x24];      // +0x54
    const char *m_Name;      // +0x78
    u32 m_DownloadSize;      // +0x7c
    u16 m_Width;             // +0x80
    u16 m_Height;            // +0x82
    u8 m_MipMapCount;        // +0x84
    u8 m_Clamp;              // +0x85
    u8 m_Filter;             // +0x86
    u8 m_Format;             // +0x87
    u64 m_ClampReg;          // +0x88
    u64 m_Miptbp1;           // +0x90
    u64 m_Miptbp2;           // +0x98
};

class rmcTextureProxyPS2 {
public:
    rmcTextureProxyPS2() : m_Pad(0), m_Ptr0x4(nullptr), m_Ptr0x8(nullptr), m_Ptr0xc(nullptr), m_Ptr0x10(nullptr) {}
    rmcTextureProxyPS2(datResource &rsc);

    u32 m_Pad;        // +0x0
    void *m_Ptr0x4;   // +0x4
    void *m_Ptr0x8;   // +0x8
    void *m_Ptr0xc;   // +0xc
    void *m_Ptr0x10;  // +0x10
};

#if !defined(_WIN64)
static_assert(sizeof(rmcTextureReference) == 16, "rmcTextureReference size mismatch");
static_assert(sizeof(rmcDictionaryReference) == 20, "rmcDictionaryReference size mismatch");
static_assert(sizeof(rmcTexturePS2) == 160, "rmcTexturePS2 size mismatch");
static_assert(sizeof(rmcTextureProxyPS2) == 20, "rmcTextureProxyPS2 size mismatch");
#endif

class rmcTextureDefault : public rmcTextureReference {
public:
    rmcTextureDefault(const char *name = nullptr);
    virtual ~rmcTextureDefault() {}
    // Consulted after the loose-file lookup fails: textures that live in a
    // page file (the licence plates in decal.ppf).  Set by the game once the
    // page file is mounted; may stay NULL.
    static class gfxTexture *(*sm_PageTextureLookup)(const char *name);
};

class rmcTextureFactory {
public:
    static rmcTextureFactory *sm_Instance;
    static rmcTextureFactory &GetInstance() { return *sm_Instance; }

    rmcTextureFactory() {}
    virtual ~rmcTextureFactory() {}

    // InitClass creates the gfx-backed default factory right away (game code
    // uses GetInstance() from the first layer load on); ShutdownClass frees it.
    static void InitClass();
    static void ShutdownClass();

    // Factory stack: a pushed factory takes over Create() until popped.
    static void PushInstance(rmcTextureFactory *inst);
    static void PushInstance(rmcTextureFactory &inst) { PushInstance(&inst); }
    static void PopInstance();

    static rmcTextureFactory *CreatePagedTextureFactory() {
        InitClass();
        return sm_Instance;
    }
    static void CreateGfxTextureFactory();
    static void CreateStringTextureFactory();
    static rmcRenderTarget *CreateRenderTarget(int w, int h, int flags = 0) { return new rmcRenderTarget(w, h); }
    virtual rmcRenderTarget *CreateRenderTarget(const char *name, int flags, int w, int h, int bpp = 32) { return new rmcRenderTarget(w, h); }

    virtual rmcTexture *Create(const char *filename);
    virtual void ResourcePageIn(class datResource &res, rmcTexture **texList, int count);
    // Console VRAM residency: textures were paged into GS memory and this
    // evicted them.  D3D owns texture memory here, so there is nothing to evict.
    virtual void FlushVram() { }
    // Mip LOD bias for the draws that follow (the console blurred the city
    // environment map's area lights by forcing a coarse mip).  Recorded for
    // readers; the D3D sampler bias is not wired up.
    virtual void SetTextureLod(float lod) { sm_TextureLod = lod; }
    static float GetTextureLod() { return sm_TextureLod; }
    virtual void LockRenderTarget(const rmcRenderTarget *rt, const rmcRenderTarget *zbuffer, class gfxViewport *vp, const class Matrix34 &cam, int face = 0);
    virtual void LockRenderTarget(const rmcRenderTarget *rt, const rmcRenderTarget *zbuffer = nullptr, class gfxViewport *vp = nullptr);
    virtual void UnlockRenderTarget(rmcRenderTarget *rt = nullptr);
    // Drop the factory's name->texture lookup cache so later Create() calls
    // re-resolve (texture streaming: called between shader groups so each
    // group's textures are paged in fresh).  This base factory keeps no cache.
    virtual void ResetCache() { }

    static void RegisterTextureReference(const char *name, const rmcTexture *tex);
    static void RegisterTextureReference(const char *name, rmcTexture *tex) {
        RegisterTextureReference(name, (const rmcTexture *)tex);
    }
    static void RegisterTextureReference(const char *name, gfxTexture *tex);
    static void RegisterTextureReference(const char *name, const char *path);
    static void DeleteTextureReference(const char *name);
    static rmcTexture *LookupTextureReference(const char *name, bool &found);
    static rmcTexture *LookupTextureReference(const char *name);

protected:
    static float sm_TextureLod;
};

// Resource build switches / tag database (console resource tools).  On PC
// packs are only read, so the name flag is inert and the tag database (the
// offline texture->page map the console tools consult) never loads.
extern bool rmcSaveTextureNames;
class rmcTagDatabaseClass {
public:
    bool Load(const char *name);
    void Kill();
    bool IsLoaded() const { return m_Loaded; }
private:
    bool m_Loaded = false;
};
extern rmcTagDatabaseClass rmcTagDatabase;

#endif // RMCORE_TEXTURE_H
