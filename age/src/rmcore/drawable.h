#ifndef RMCORE_DRAWABLE_H
#define RMCORE_DRAWABLE_H

#include "core/output.h"
#include "atl/string.h"
#include "vector/Matrix34.h"
#include "data/assetcfg.h"
#include "rmcore/shader.h"
#include "rmcore/lodgroup.h"
#include "rmcore/typefileparser.h"
#include "gfx/simple.h"
#include "atl/bitset.h"
#include "data/resource.h"
#include <string>

class datTokenizer;
class crSkeletonData;
class gfxEdgeModel;
class gfxModel;
class crSkeleton;
class rmcGeometry;

class rmcDrawableBase {
public:
    rmcDrawableBase();
    rmcDrawableBase(datResource &rsc);
    virtual ~rmcDrawableBase();

    virtual void Delete() { delete this; }
    virtual bool Load(const char *basename, rmcTypeFileParser *parser = nullptr, bool configParser = true) { (void)basename; (void)parser; (void)configParser; return false; }
    virtual bool Load(datTokenizer &tok, rmcTypeFileParser *parser = nullptr, bool configParser = true) { (void)tok; (void)parser; (void)configParser; return false; }
    virtual bool LoadNewFormat(datTokenizer &tok) { (void)tok; return false; }
    virtual bool LoadFromParser(datTokenizer &tok, rmcTypeFileParser *parser, bool configParser) { (void)tok; (void)parser; (void)configParser; return false; }
    virtual void Draw(const rmcShaderData *data, const Matrix34 &mtx, int bucket, int lod, atBitSet *enables = nullptr, int variant = 0) const { Quitf("rmcDrawableBase::Draw - not implemented"); }
    virtual void DrawCpv(const rmcShaderData *data, const Matrix34 &mtx, int bucket, int lod, int cpvIndex, int variant = 0) const { Quitf("rmcDrawableBase::DrawCpv - not implemented"); }
    virtual void DrawSkinned(const rmcShaderData *data, const class crSkeleton &skel, int bucket, int lod, int variant = 0) const { Quitf("rmcDrawableBase::DrawSkinned - not implemented"); }
    virtual gfxCullStatus IsVisible(const Matrix34 &m, const class gfxViewport &vp, u8 &lod, float *dist) const;
    virtual gfxCullStatus IsVisible(const Matrix34 &m, const class gfxViewport &vp, u8 &lod) const { return IsVisible(m, vp, lod, nullptr); }
    virtual unsigned int GetBucketMask(int pass = 0) const { (void)pass; return 0xFFFFFFFF; }

    rmcShaderGroup *GetShaderGroups() const { return m_ShaderGroups; }
    int GetShaderGroupCount() const { return m_ShaderGroupCount ? m_ShaderGroupCount : 1; }
    // Where in the resource image this drawable was read from, 0 when it was
    // not.  The geometry is not part of the object: it is VIF packets hanging
    // off the lod tables further into the image, so anything that wants to
    // build it has to know the address it came from.
    u32 GetResourceAddr() const { return m_ResourceAddr; }

protected:
    u8 m_ShaderGroupCount;          // +0x4
    u8 m_Pad;                       // +0x5
    u16 m_Flags;                    // +0x6
    rmcShaderGroup *m_ShaderGroups; // +0x8
    rmcShaderData *m_ShaderData;    // +0xc
    u32 m_ResourceAddr;             // PC only; not part of the image layout
};

class rmcDrawable : public rmcDrawableBase {
public:
    rmcDrawable();
    rmcDrawable(datResource &rsc);
    virtual ~rmcDrawable();

    virtual bool Load(const char *filename);
    virtual bool Load(datTokenizer &tok);
    virtual bool Load(const char *basename, rmcTypeFileParser *parser, bool configParser = true) override;
    virtual bool Load(datTokenizer &tok, rmcTypeFileParser *parser, bool configParser = true) override;

    virtual void LoadMesh(rmcTypeFileCbData *data);
    virtual void LoadSkel(rmcTypeFileCbData *data);
    virtual void LoadShader(rmcTypeFileCbData *data);
    virtual void LoadEdgeModel(rmcTypeFileCbData *data);
    virtual void Reset() { Quitf("rmcDrawable::Reset - not implemented"); }

    virtual crSkeletonData* GetSkeletonData();
    // Adopt a skeleton the drawable did not load itself.  The rider's rig comes
    // out of a page file rather than beside its mesh, so something has to hand
    // it over after the fact.
    void SetSkeletonData(crSkeletonData *skel) { m_SkeletonData = skel; }
    virtual gfxEdgeModel* GetEdgeModel();
    virtual rmcShaderGroup & GetShaderGroup(int index = 0) const;
    int GetShaderGroupCount() const { return m_ShaderGroupCount ? m_ShaderGroupCount : 1; }
    virtual rmcShaderData* GetShaderData() const;
    static unsigned int sm_BucketMask;
    static void AddWidgets(class bkBank &bank);
    static void InitBank();
    static void SetWarningSpew(bool spew) { Quitf("rmcDrawable::SetWarningSpew - not implemented"); }

    virtual unsigned int GetBucketMask() const;
    virtual unsigned int GetBucketMask(int pass) const override;

    void SetBoundingBox(const Vector3 &min, const Vector3 &max) { m_LodGroup.SetBoundingBox(min, max); }
    void GetBoundingBox(Vector3 &min, Vector3 &max) const { m_LodGroup.GetBoundingBox(min, max); }

    static bool sm_PassEnable[32];
    static bool GetPassEnable(int pass) { return true; }
    static void SetPassEnable(int pass, bool enable) { Quitf("rmcDrawable::SetPassEnable - not implemented"); }

    rmcLodGroup &GetLodGroup() { return m_LodGroup; }
    const rmcLodGroup &GetLodGroup() const { return m_LodGroup; }

    void SetModel(int lod, gfxModel* m) { m_LodGroup.SetModel(lod, m); }
    gfxModel* GetModel(int lod = 0) const;

    virtual gfxCullStatus IsVisible(const Matrix34 &m, const class gfxViewport &vp, u8 &lod, float *dist) const override;
    virtual gfxCullStatus IsVisible(const Matrix34 &m, const class gfxViewport &vp, u8 &lod) const override;
    virtual void Draw(int pass, rmcShaderData* sd, const Matrix34 &m, int pass2, int flag) const;
    virtual void Draw(const class rmcShaderGroup &shaderGroup, rmcShaderData *sd, int bucket, int arg3) const;
    virtual void Draw(const class rmcShaderGroup &shaderGroup, rmcShaderData *sd, const Matrix34 *mtx, int bucket, int lod, atBitSet *enables = nullptr, int variant = 0) const;
    virtual void Draw(const rmcShaderData *data, const Matrix34 &mtx, int bucket, int lod, atBitSet *enables = nullptr, int variant = 0) const override;
    virtual void DrawCpv(const rmcShaderData *data, const Matrix34 &mtx, int bucket, int lod, int cpvIndex, int variant = 0) const override;
    virtual void DrawSkinned(int pass, rmcShaderData* sd, const crSkeleton &skel, int pass2, int flag) const;
    virtual void DrawSkinned(const rmcShaderData *data, const crSkeleton &skel, int bucket, int lod, int variant = 0) const override;
    virtual void DrawSkinned(const class rmcShaderGroup &shaderGroup, rmcShaderData *sd, const Matrix34 *mtx, int numMtx, int bucket, int arg) const;
    virtual void DrawCpv(const class rmcShaderGroup &shaderGroup, rmcShaderData *sd, int bucket, int lod, int cpvIndex) const;
    virtual int GetMatrixCount() const { return 0; }
    virtual void Delete() override { delete this; }

    static rmcShaderGroup *s_ShaderGroup;
    static int s_ShaderGroupCount;

protected:
    rmcLodGroup m_LodGroup;             // +0x10..+0x67 (88 bytes)
    crSkeletonData* m_SkeletonData;     // +0x68 (4 bytes)
    gfxEdgeModel* m_EdgeModel;          // +0x6c (4 bytes)
    u32 m_Unk_0x70;                     // +0x70 (4 bytes)
    u32 m_EdgeModelSize;                // +0x74 (4 bytes)
    // PC-only bookkeeping used by the game's drwShaderModel (mcgfx/drawable.c):
    // the type-file base name and a fallback bound radius.  Not part of the PS2 image.
    atString m_Name;
    float m_Radius = 0.0f;
};

struct rmcModelInfo {};
class mshMesh;

class rmcModel {
public:
    rmcModel(unsigned char flags = 0);
    rmcModel(datResource &rsc);
    virtual ~rmcModel();

    virtual void Draw(const rmcShaderGroup &shaders, const rmcShaderData *data, int bucket, int lod) const;
    virtual void DrawCpv(const rmcShaderGroup &shaders, const rmcShaderData *data, int bucket, int lod, int cpvIndex) const;
    virtual void DrawSkinned(const rmcShaderGroup &shaders, const rmcShaderData *data, const Matrix34 *mtxs, int mtxCount, int bucket, int lod, const atBitSet *enables = nullptr) const;
    virtual unsigned int ComputeBucketMask(const rmcShaderGroup &shaders) const;
    virtual unsigned int GetBucketMask() const;
    virtual void Delete() { delete this; }

    virtual bool Load(const char *name);

    void SetModel(int lod, class gfxModel *m) { (void)lod; m_GfxModel = m; }
    class gfxModel *GetModel(int lod = 0) const { (void)lod; return m_GfxModel; }
    void SetBoundingBox(const Vector3 &min, const Vector3 &max);
    void GetBoundingBox(Vector3 &min, Vector3 &max) const;

    static rmcModel* Create(const char *name);
    static rmcModel* Create(const char *type, const char *name, int flags, int fvf);
    static rmcModel* Create(rmcShaderGroup &shaderGroup, const char *name, int arg1 = 0, int arg2 = 0, bool arg3 = false, rmcModelInfo *info = nullptr);
    static rmcModel* Create(rmcShaderGroup &shaderGroup, const class mshMesh &mesh, int arg1 = 0, int arg2 = 0, bool arg3 = false, rmcModelInfo *info = nullptr, bool arg4 = false, const char *name = nullptr, bool arg5 = false);

    static class Stream *(*OpenCpvFile)(const char *basename);
    static void SetPositionFractionalBits(int bits) { sm_PositionFractionalBits = bits; }
    static int GetPositionFractionalBits() { return sm_PositionFractionalBits; }
    static int sm_PositionFractionalBits;
    static int sm_ForceShader;

    void *GetPackets() const { return m_Packets; }
    u16 GetPacketCount() const { return m_NumPackets; }
    // Skinning matrix count of the PC model (0 = rigid); game code branches on it
    // between Draw and DrawSkinned (mcprop/proptype.c, mcplayer/animobj.c).
    int GetMatrixCount() const;

protected:
    u32 m_Flags;                  // +0x4
    u16 m_NumPackets;             // +0x8
    u16 m_Flags2;                 // +0xa
    union {
        void *m_Packets;          // +0xc
        class gfxModel *m_GfxModel;
    };
};

class rmcModelGeom : public rmcModel {
public:
    rmcModelGeom();
    rmcModelGeom(datResource &rsc);
    virtual ~rmcModelGeom();

    virtual void Draw(const rmcShaderGroup &shaders, const rmcShaderData *data, int bucket, int lod) const override;
    virtual void DrawCpv(const rmcShaderGroup &shaders, const rmcShaderData *data, int bucket, int lod, int cpvIndex) const override;
    virtual void DrawSkinned(const rmcShaderGroup &shaders, const rmcShaderData *data, const Matrix34 *mtxs, int mtxCount, int bucket, int lod, const atBitSet *enables = nullptr) const override;
    virtual bool Load(class mshMesh &mesh, bool instanceCpv, const char *name);

    rmcGeometry *GetGeometry() const { return m_Geometry; }

protected:
    rmcGeometry *m_Geometry; // +0x10
    void *m_ColorData;       // +0x14
};

#if !defined(_WIN64)
static_assert(sizeof(rmcDrawableBase) == 16, "rmcDrawableBase size mismatch");
static_assert(sizeof(rmcDrawable) == 120, "rmcDrawable size mismatch");
static_assert(sizeof(rmcModel) == 16, "rmcModel size mismatch");
static_assert(sizeof(rmcModelGeom) == 24, "rmcModelGeom size mismatch");
#endif

#include "phcore/segment.h"
#include "phcore/isect.h"
#include "phbound/bound.h"

#endif // RMCORE_DRAWABLE_H
