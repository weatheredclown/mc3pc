#ifndef RMCORE_LODGROUP_H
#define RMCORE_LODGROUP_H

#include "core/output.h"
#include "core/types.h"
#include "data/resource.h"
#include "vector/vector3.h"

enum eLodType {
    LOD_HIGH = 0,
    LOD_MED = 1,
    LOD_LOW = 2,
    LOD_VLOW = 3,
    LOD_COUNT = 4
};

class rmcModel;

class rmcLod {
public:
    u8 m_Type;        // +0x0
    u8 m_Flags;       // +0x1
    u16 m_ModelCount; // +0x2

    rmcLod() : m_Type(0), m_Flags(0), m_ModelCount(0) {}
    virtual ~rmcLod() {}

    virtual rmcModel *GetModel(int index = 0) const { Quitf("rmcLod::GetModel - not implemented"); return nullptr; }
    virtual void SetModel(int index, rmcModel *m) { (void)index; (void)m; }
    void SetModel(rmcModel *m) { SetModel(0, m); }

    static rmcLod *ResourcePageIn(datResource &rsc);
};

class rmcLodStatic : public rmcLod {
public:
    rmcLodStatic(int count = 1);
    rmcLodStatic(datResource &rsc);
    virtual ~rmcLodStatic();

    virtual rmcModel *GetModel(int index = 0) const override;
    virtual void SetModel(int index, rmcModel *m) override;

protected:
    rmcModel **m_Models; // +0x8 (4 bytes)
};

class rmcLodPaged : public rmcLod {
public:
    rmcLodPaged(int count = 1);
    rmcLodPaged(datResource &rsc);
    virtual ~rmcLodPaged();

    virtual rmcModel *GetModel(int index = 0) const override;
    virtual void SetModel(int index, rmcModel *m) override;

protected:
    rmcModel **m_Models; // +0x8 (4 bytes)
    void *m_PagedData1;  // +0xc (4 bytes)
    void *m_PagedData2;  // +0x10 (4 bytes)
};

class rmcLodStreamed : public rmcLod {
public:
    rmcLodStreamed(int count = 1);
    rmcLodStreamed(datResource &rsc);
    virtual ~rmcLodStreamed();

    virtual rmcModel *GetModel(int index = 0) const override;
    virtual void SetModel(int index, rmcModel *m) override;

protected:
    rmcModel **m_Models; // +0x8 (4 bytes)
};

class rmcLodGroup {
public:
    rmcLodGroup();
    rmcLodGroup(datResource &rsc);
    ~rmcLodGroup();

    void SetLodThresh(int lod, float thresh) { if (lod >= 0 && lod < LOD_COUNT) m_Thresholds[lod] = thresh; }
    float GetLodThresh(int lod) const { return (lod >= 0 && lod < LOD_COUNT) ? m_Thresholds[lod] : 0.0f; }

    rmcLod &GetLod(int lod);
    const rmcLod &GetLod(int lod) const;
    rmcModel *GetModel(int lod = 0) const;
    void SetModel(int lod, rmcModel *m);
    void SetModel(int lod, class gfxModel *m);

    void GetBoundingBox(Vector3 &boxMin, Vector3 &boxMax) const {
        boxMin = m_Min;
        boxMax = m_Max;
    }
    void SetBoundingBox(const Vector3 &boxMin, const Vector3 &boxMax) {
        m_Min = boxMin;
        m_Max = boxMax;
    }

    float GetCullRadius() const { return m_Radius > 0.0f ? m_Radius : 1000.0f; }
    void SetCullRadius(float r) { m_Radius = r; }

    int ComputeLod(float zDist) const { return LOD_HIGH; }

    void Draw(const class rmcShaderGroup &shaderGroup, const union rmcShaderData *data, int bucket, int lod, class atBitSet *enables = nullptr, int variant = 0) const;
    void Draw(const class rmcShaderGroup &shaderGroup, const union rmcShaderData *data, int bucket, int lod) const;
    void Draw(const class rmcShaderGroup &shaderGroup, const union rmcShaderData *data, const class Matrix34 *mtx, int bucket, int lod, class atBitSet *enables = nullptr, int variant = 0) const;
    void Draw(const class rmcShaderGroup &shaderGroup, const union rmcShaderData *data, const class Matrix34 *mtx, int bucket, int lod) const;

public:
    rmcLod *m_Lods[LOD_COUNT];        // +0x00..+0x0f (16 bytes)
    float m_Thresholds[LOD_COUNT];    // +0x10..+0x1f (16 bytes)
    u32 m_BucketMasks[LOD_COUNT];     // +0x20..+0x2f (16 bytes)
    Vector3 m_Center;                 // +0x30..+0x3b (12 bytes)
    float m_Radius;                   // +0x3c..+0x3f (4 bytes)
    Vector3 m_Min;                    // +0x40..+0x4b (12 bytes)
    Vector3 m_Max;                    // +0x4c..+0x57 (12 bytes)
    bool m_FromResource;
};

#if !defined(_WIN64)
static_assert(sizeof(rmcLodGroup) == 88, "rmcLodGroup size mismatch");
static_assert(sizeof(rmcLodStatic) == 12, "rmcLodStatic size mismatch");
static_assert(sizeof(rmcLodPaged) == 20, "rmcLodPaged size mismatch");
static_assert(sizeof(rmcLodStreamed) == 12, "rmcLodStreamed size mismatch");
#endif

#endif // RMCORE_LODGROUP_H
