#ifndef RMCORE_SHADER_H
#define RMCORE_SHADER_H

#include "core/output.h"
#include "core/types.h"
#include "data/resource.h"
#include "vector/matrix34.h"
#include "rmcore/state.h"

class rmcModel;
class rmcGeometry;
class rmcGeometryCpv;
class rmcShader;

union rmcShaderData {
    int Int;
    float Float;
    void *Ptr;

    rmcShaderData() : Int(0) {}
    rmcShaderData(int i) : Int(i) {}
    rmcShaderData(float f) : Float(f) {}
    rmcShaderData(void *p) : Ptr(p) {}
};

class rmcShaderGroup {
public:
    rmcShaderGroup(int count = 0);
    rmcShaderGroup(datResource &rsc);
    virtual ~rmcShaderGroup();

    int GetLocalCount() const;
    void AddWidgets(class bkBank &bank);
    // Bank widgets for this group's shaders plus a model's local overrides.
    void AddWidgets(class bkBank &bank, rmcShaderData *localShaders) { AddWidgets(bank); (void)localShaders; }

    // Reserve room for `count` shaders before a run of Add() (city shader
    // type files declare the count up front).  Drops any existing entries.
    void InitCount(int count);
    void Add(rmcShader *shader);
    int GetCount() const { return m_Count; }
    rmcShader &operator[](int index);
    const rmcShader &operator[](int index) const;
    rmcShader *GetShader(int index) const;
    void SetShader(int index, rmcShader *shader);

    int LookupLocal(const char *name) const;
    int AddLocal(const char *name, float defaultVal = 0.0f) { Quitf("Missing impl in shader.h"); return 0; }
    int AddLocal(const char *name, int defaultVal) { Quitf("Missing impl in shader.h"); return 0; }
    // Per-instance storage for this group's shader locals.  Two models sharing
    // a shader group each get their own copy, which is how one car can be
    // faded out while another is not.  The caller owns the block and hands it
    // back to FreeLocals.
    rmcShaderData *AllocateLocals() const;
    void FreeLocals(rmcShaderData *data = nullptr) const;
    void ResourcePageIn(datResource &rsc);

    static rmcShaderGroup *sm_ParentShaderGroup;
    static int sm_ParentShaderCount;
    static rmcShaderGroup *s_ParentShaderGroup;
    static int s_ParentShaderCount;

protected:
    rmcShader **m_Shaders; // +0x4
    u16 m_Count;          // +0x8
    u16 m_Capacity;       // +0xa
    void *m_Locals;       // +0xc
};

class rmcShader {
public:
    static rmcShaderData sm_Globals[64];
    static char sm_DrawBucketNames[32][64];
    static u32 sm_PassEnable;

    static u32 &GetPassEnable() { return sm_PassEnable; }
    static bool GetPassEnable(int pass) { return (sm_PassEnable & (1 << pass)) != 0; }
    static void SetPassEnable(u32 mask) { sm_PassEnable = mask; }
    static void SetPassEnable(int pass, bool enable) {
        if (enable) sm_PassEnable |= (1 << pass);
        else sm_PassEnable &= ~(1 << pass);
    }

    static void SetGlobal(int index, float val);
    static int LookupGlobal(const char *name);

    rmcShader();
    rmcShader(datResource &rsc);
    virtual ~rmcShader();

    virtual void Load(const char *name, const char * const *args, int numArgs, rmcShaderGroup &group) { Quitf("rmcShader::Load - not implemented"); }
    virtual void Load(const char *name, const char **args, int numArgs, rmcShaderGroup &group) { Quitf("rmcShader::Load - not implemented"); }
    virtual void Load(const char *name, char **args, int numArgs, rmcShaderGroup &group) { Quitf("rmcShader::Load - not implemented"); }
    virtual void Load(const char *name, const char * const *args, int numArgs, const rmcShaderGroup &group) { Quitf("rmcShader::Load - not implemented"); }
    virtual void Load(const char *name, const char **args, int numArgs, const rmcShaderGroup &group) { Quitf("rmcShader::Load - not implemented"); }
    virtual void Load(const char *name, char **args, int numArgs, const rmcShaderGroup &group) { Quitf("rmcShader::Load - not implemented"); }
    virtual bool Load(const char *name, const char * const *args1 = nullptr, int arg2 = 0, int arg3 = 0, const rmcShaderGroup &group = *(const rmcShaderGroup*)nullptr);
    virtual bool Load(const char *name, char **args1, int arg2, int arg3, const rmcShaderGroup &group = *(const rmcShaderGroup*)nullptr);
    virtual bool Load(const char *name, const char **args1, int arg2, int arg3, const rmcShaderGroup &group = *(const rmcShaderGroup*)nullptr);
    static void SetTemplatePath(const char *path);
    static void SetDrawBucketName(int bucket, const char *name);
    static const char *GetDrawBucketName(int bucket);

    virtual void Bind(const rmcShaderData *locals, const rmcShaderData *globals, int pass = 0) const { Quitf("rmcShader::Bind - not implemented"); }
    virtual void Draw(const rmcModel &model, const rmcShaderData *data, const rmcGeometry &geom, int lod) const { Quitf("rmcShader::Draw - not implemented"); }
    virtual void DrawCpv(const rmcModel &model, const rmcShaderData *data, const rmcGeometry &geom, int lod, const rmcGeometryCpv &cpv) const { Quitf("rmcShader::DrawCpv - not implemented"); }
    virtual void DrawSkinned(const rmcModel &model, const rmcShaderData *data, const rmcGeometry &geom, const Matrix34 *mtxs, int mtxCount, int lod) const { Quitf("rmcShader::DrawSkinned - not implemented"); }
    virtual void RestoreStates() const { Quitf("rmcShader::RestoreStates - not implemented"); }
    virtual void ResourcePageIn(datResource &rsc) { Quitf("rmcShader::ResourcePageIn - not implemented"); }

    int AddLocal(const char *name, float defaultVal = 0.0f) { Quitf("rmcShader::AddLocal - not implemented"); return 0; }
    int AddLocal(const char *name, int defaultVal) { Quitf("rmcShader::AddLocal - not implemented"); return 0; }
    // PC-only: the game shader subclasses (mcgfx/mcShader.c, mclevel/shaders.cpp) record
    // their mcShaderTypes / city shader kind here.  Not part of the PS2 image.
    int m_Type = 0;

    struct Waveform {
        enum Func { LINEAR = 0, SIN = 1, COS = 2 };
        Func m_Func;
        rmcShaderData m_A;
        rmcShaderData m_D;
    };

    struct TextureMatrix {
        Waveform *m_ScaleS;  // +0x0
        Waveform *m_ScaleT;  // +0x4
        Waveform *m_TransS;  // +0x8
        Waveform *m_TransT;  // +0xc
        Waveform *m_Rot;     // +0x10

        TextureMatrix() : m_ScaleS(nullptr), m_ScaleT(nullptr), m_TransS(nullptr), m_TransT(nullptr), m_Rot(nullptr) {}
        TextureMatrix(datResource &rsc);
    };

    class Pass {
    public:
        class Stage {
        public:
            union {
                struct {
                    class rmcTexture *m_Texture;
                    class rmcTexture *m_Texture2;
                };
                class rmcTexture *m_Textures[2];
            };
            void *m_EnvMap;
            TextureMatrix *m_TexMtx;
            u8 m_Flags;
            u8 m_TexCoordIndex;
            u8 m_Filter;
            u8 m_TextureCount;

            Stage();
            Stage(datResource &rsc);
            void Bind(int stage, const rmcShaderData *locals, const rmcShaderData *globals) const { Quitf("Stage::Bind - not implemented"); }
        };
        void *m_State;
        u8 m_Flags;
        u8 m_Blend;
        u16 m_Pad;
        Stage m_FirstStage;
        Pass *m_NextPass;

        Pass();
        Pass(datResource &rsc);
    };

    virtual Pass &GetFirstPass();
    int m_DrawBucket; // +0x4
    int GetType() const { return 0; }
    int GetDrawBucket() const { return m_DrawBucket; }
};

class rmcShaderInstance : public rmcShader {
public:
    rmcShaderInstance();
    rmcShaderInstance(class datResource &rsc);
    virtual ~rmcShaderInstance();
    virtual void Load(const char *templateName, const char * const *args, int numArgs, class rmcShaderGroup &group) { Quitf("rmcShaderInstance::Load - not implemented"); }
};

// A small numbered table of .shadert templates the level keeps resident while
// it builds shaders.  Load pins the named template's text in the template
// cache under a slot id; Unload releases the slot.  Holding them by id is what
// stops the cache being emptied out from under a level that is still
// instantiating shaders from them.
class rmcShaderTemplate {
public:
    enum { MAX_TEMPLATES = 16 };

    static void Load(int id, const char *name);
    static void Unload(int id);
    // Text of the template in `id`, or NULL when that slot is empty.
    static const char *Get(int id);
    static const char *GetName(int id);
};

#include "rmcore/shadertemplate.h"   // rmcShadertCache (.shadert text cache)

class rmcShaderFactory {
public:
    static rmcShaderFactory *sm_Instance;
    static rmcShaderFactory &GetInstance() { return *sm_Instance; }

    rmcShaderFactory() {}
    virtual ~rmcShaderFactory() {}

    static void InitClass();
    static void ShutdownClass();
    static rmcShaderFactory *CreateStandardShaderFactory();
    static void PushInstance(rmcShaderFactory *inst = nullptr);
    static void PushInstance(rmcShaderFactory &inst) { PushInstance(&inst); }
    static void PopInstance();

    virtual rmcShader *Create(const char *filename, int argCount = 0, const char **args = nullptr);
    virtual rmcShaderGroup *CreateGroup(int count);
    virtual void ResourcePageIn(datResource &rsc, rmcShader **shaders, int count);
    void ResourcePageIn(datResource &rsc, rmcShaderGroup *groups);
};

#if !defined(_WIN64)
static_assert(sizeof(rmcShaderGroup) == 16, "rmcShaderGroup size mismatch");
static_assert(sizeof(rmcShader) == 8, "rmcShader size mismatch");
static_assert(sizeof(rmcShader::TextureMatrix) == 20, "TextureMatrix size mismatch");
static_assert(sizeof(rmcShader::Pass) == 32, "Pass size mismatch");
static_assert(sizeof(rmcShader::Pass::Stage) == 20, "Stage size mismatch");
#endif

#endif // RMCORE_SHADER_H
