#include "rmcore/lodgroup.h"
#include "rmcore/drawable.h"
#include "data/memory.h"

// ---------------------------------------------------------------------------
// rmcLod & polymorphic page-in
// ---------------------------------------------------------------------------

rmcLod *rmcLod::ResourcePageIn(datResource &rsc)
{
    // Byte 0 holds the lod type tag in PS2 binary
    u8 type = rsc.GetU8();
    rsc.Seek(rsc.Tell() - 1);
    if (type == 0) {
        return new rmcLodStatic(rsc);
    } else if (type == 2) {
        return new rmcLodPaged(rsc);
    } else {
        return new rmcLodStreamed(rsc);
    }
}

// ---------------------------------------------------------------------------
// rmcLodStatic
// ---------------------------------------------------------------------------

rmcLodStatic::rmcLodStatic(int count)
    : m_Models(nullptr)
{
    m_Type = 0;
    m_Flags = 0;
    m_ModelCount = (u16)count;
    if (count > 0) {
        m_Models = new rmcModel*[count];
        for (int i = 0; i < count; ++i) m_Models[i] = nullptr;
    }
}

rmcLodStatic::rmcLodStatic(datResource &rsc)
    : m_Models(nullptr)
{
#if defined(__WIN32PC)
    u32 start = rsc.Tell();
    m_Type = rsc.GetU8();
    m_Flags = rsc.GetU8();
    m_ModelCount = rsc.GetU16();
    rsc.GetVTable();
    rsc.PointerFixup(m_Models);
    u32 consumed = rsc.Tell() - start;
    (void)consumed;
#else
    rsc.PointerFixup(m_Models);
#endif
}

rmcLodStatic::~rmcLodStatic()
{
    delete [] m_Models;
}

rmcModel *rmcLodStatic::GetModel(int index) const
{
    return (m_Models && index >= 0 && index < (int)m_ModelCount) ? m_Models[index] : nullptr;
}

void rmcLodStatic::SetModel(int index, rmcModel *m)
{
    if (!m_Models && m_ModelCount == 0) {
        m_ModelCount = 1;
        m_Models = new rmcModel*[1];
        m_Models[0] = nullptr;
    }
    if (m_Models && index >= 0 && index < (int)m_ModelCount) {
        m_Models[index] = m;
    }
}

// ---------------------------------------------------------------------------
// rmcLodPaged
// ---------------------------------------------------------------------------

rmcLodPaged::rmcLodPaged(int count)
    : m_Models(nullptr), m_PagedData1(nullptr), m_PagedData2(nullptr)
{
    m_Type = 2;
    m_Flags = 0;
    m_ModelCount = (u16)count;
    if (count > 0) {
        m_Models = new rmcModel*[count];
        for (int i = 0; i < count; ++i) m_Models[i] = nullptr;
    }
}

rmcLodPaged::rmcLodPaged(datResource &rsc)
    : m_Models(nullptr), m_PagedData1(nullptr), m_PagedData2(nullptr)
{
#if defined(__WIN32PC)
    u32 start = rsc.Tell();
    m_Type = rsc.GetU8();
    m_Flags = rsc.GetU8();
    m_ModelCount = rsc.GetU16();
    rsc.GetVTable();
    rsc.PointerFixup(m_Models);
    rsc.PointerFixup(m_PagedData1);
    rsc.PointerFixup(m_PagedData2);
    u32 consumed = rsc.Tell() - start;
    (void)consumed;
#else
    rsc.PointerFixup(m_Models);
    rsc.PointerFixup(m_PagedData1);
    rsc.PointerFixup(m_PagedData2);
#endif
}

rmcLodPaged::~rmcLodPaged()
{
    delete [] m_Models;
}

rmcModel *rmcLodPaged::GetModel(int index) const
{
    return (m_Models && index >= 0 && index < (int)m_ModelCount) ? m_Models[index] : nullptr;
}

void rmcLodPaged::SetModel(int index, rmcModel *m)
{
    if (m_Models && index >= 0 && index < (int)m_ModelCount) {
        m_Models[index] = m;
    }
}

// ---------------------------------------------------------------------------
// rmcLodStreamed
// ---------------------------------------------------------------------------

rmcLodStreamed::rmcLodStreamed(int count)
    : m_Models(nullptr)
{
    m_Type = 1;
    m_Flags = 0;
    m_ModelCount = (u16)count;
    if (count > 0) {
        m_Models = new rmcModel*[count];
        for (int i = 0; i < count; ++i) m_Models[i] = nullptr;
    }
}

rmcLodStreamed::rmcLodStreamed(datResource &rsc)
    : m_Models(nullptr)
{
#if defined(__WIN32PC)
    u32 start = rsc.Tell();
    m_Type = rsc.GetU8();
    m_Flags = rsc.GetU8();
    m_ModelCount = rsc.GetU16();
    rsc.GetVTable();
    rsc.PointerFixup(m_Models);
    u32 consumed = rsc.Tell() - start;
    (void)consumed;
#else
    rsc.PointerFixup(m_Models);
#endif
}

rmcLodStreamed::~rmcLodStreamed()
{
    delete [] m_Models;
}

rmcModel *rmcLodStreamed::GetModel(int index) const
{
    return (m_Models && index >= 0 && index < (int)m_ModelCount) ? m_Models[index] : nullptr;
}

void rmcLodStreamed::SetModel(int index, rmcModel *m)
{
    if (m_Models && index >= 0 && index < (int)m_ModelCount) {
        m_Models[index] = m;
    }
}

// ---------------------------------------------------------------------------
// rmcLodGroup
// ---------------------------------------------------------------------------

rmcLodGroup::rmcLodGroup()
    : m_FromResource(false)
{
    for (int i = 0; i < LOD_COUNT; ++i) {
        m_Lods[i] = nullptr;
        m_Thresholds[i] = (i + 1) * 50.0f;
        m_BucketMasks[i] = 0xFFFFFFFF;
    }
    m_Center.Zero();
    m_Radius = 0.0f;
    m_Min.Zero();
    m_Max.Zero();
}

rmcLodGroup::rmcLodGroup(datResource &rsc)
#if defined(__WIN32PC)
    // Nothing in the image survives into m_Lods here (see below), so every lod
    // this group ends up with is one it allocated and has to free.
    : m_FromResource(false)
#else
    : m_FromResource(true)
#endif
{
#if defined(__WIN32PC)
    u32 start = rsc.Tell();
    // Each lod has to be BUILT, not just pointed at.  PointerFixup leaves the
    // image address in the slot, which is not a usable rmcLod: GetLod would
    // hand that address straight back (it only allocates when the slot is
    // null), GetModel would read through it, and the destructor would delete
    // it.  rmcLod::ResourcePageIn reads the type tag and constructs the right
    // subclass, which is what makes a pack-loaded drawable able to produce a
    // model at all - without it a resourced ped or vehicle drawable has lods
    // that cannot be drawn.
    for (int i = 0; i < LOD_COUNT; ++i) {
        rsc.PointerFixup(m_Lods[i]);
        // Read the slot to keep the cursor in step, then drop it.  What comes
        // back is the lod's address in the image, not an rmcLod: GetLod would
        // hand that address straight back (it only allocates when the slot is
        // null), GetModel would read through it and the destructor would delete
        // it.  The real lods are built from the image afterwards, by
        // rscLoadDrawableFromResource decoding the packets each one points at.
        m_Lods[i] = nullptr;
    }
    for (int i = 0; i < LOD_COUNT; ++i) {
        m_Thresholds[i] = rsc.GetFloat();
    }
    for (int i = 0; i < LOD_COUNT; ++i) {
        m_BucketMasks[i] = rsc.GetU32();
    }
    m_Center.x = rsc.GetFloat();
    m_Center.y = rsc.GetFloat();
    m_Center.z = rsc.GetFloat();
    m_Radius = rsc.GetFloat();
    m_Min.x = rsc.GetFloat();
    m_Min.y = rsc.GetFloat();
    m_Min.z = rsc.GetFloat();
    m_Max.x = rsc.GetFloat();
    m_Max.y = rsc.GetFloat();
    m_Max.z = rsc.GetFloat();
    u32 consumed = rsc.Tell() - start;
    (void)consumed;
#else
    for (int i = 0; i < LOD_COUNT; ++i) {
        rsc.PointerFixup(m_Lods[i]);
    }
#endif
}

rmcLodGroup::~rmcLodGroup()
{
    if (!m_FromResource) {
        for (int i = 0; i < LOD_COUNT; ++i) {
            delete m_Lods[i];
            m_Lods[i] = nullptr;
        }
    }
}

rmcLod &rmcLodGroup::GetLod(int lod)
{
    int idx = (lod >= 0 && lod < LOD_COUNT) ? lod : 0;
    if (!m_Lods[idx]) {
        m_Lods[idx] = new rmcLodStatic(1);
    }
    return *m_Lods[idx];
}

const rmcLod &rmcLodGroup::GetLod(int lod) const
{
    int idx = (lod >= 0 && lod < LOD_COUNT) ? lod : 0;
    if (m_Lods[idx]) return *m_Lods[idx];
    static rmcLodStatic s_dummy(0);
    return s_dummy;
}

rmcModel *rmcLodGroup::GetModel(int lod) const
{
    int idx = (lod >= 0 && lod < LOD_COUNT) ? lod : 0;
    if (m_Lods[idx] && m_Lods[idx]->GetModel(0))
        return m_Lods[idx]->GetModel(0);
    for (int i = idx - 1; i >= 0; i--) {
        if (m_Lods[i] && m_Lods[i]->GetModel(0))
            return m_Lods[i]->GetModel(0);
    }
    for (int i = idx + 1; i < LOD_COUNT; i++) {
        if (m_Lods[i] && m_Lods[i]->GetModel(0))
            return m_Lods[i]->GetModel(0);
    }
    return nullptr;
}

void rmcLodGroup::SetModel(int lod, rmcModel *m)
{
    if (lod >= 0 && lod < LOD_COUNT) {
        if (!m_Lods[lod]) m_Lods[lod] = new rmcLodStatic(1);
        m_Lods[lod]->SetModel(0, m);
    }
}

void rmcLodGroup::SetModel(int lod, class gfxModel *m)
{
    if (lod >= 0 && lod < LOD_COUNT) {
        rmcModel *mdl = GetModel(lod);
        if (!mdl) {
            mdl = new rmcModel();
            SetModel(lod, mdl);
        }
        mdl->SetModel(0, m);
    }
}

void rmcLodGroup::Draw(const class rmcShaderGroup &shaderGroup, const union rmcShaderData *data, int bucket, int lod, class atBitSet *enables, int variant) const
{
    (void)enables; (void)variant;
    rmcModel *m = GetModel(lod);
    if (m) m->Draw(shaderGroup, data, bucket, lod);
}

void rmcLodGroup::Draw(const class rmcShaderGroup &shaderGroup, const union rmcShaderData *data, int bucket, int lod) const
{
    rmcModel *m = GetModel(lod);
    if (m) m->Draw(shaderGroup, data, bucket, lod);
}

void rmcLodGroup::Draw(const class rmcShaderGroup &shaderGroup, const union rmcShaderData *data, const class Matrix34 *mtx, int bucket, int lod, class atBitSet *enables, int variant) const
{
    (void)variant;
    rmcModel *m = GetModel(lod);
    if (m) m->DrawSkinned(shaderGroup, data, mtx, 512, bucket, lod, enables);
}

void rmcLodGroup::Draw(const class rmcShaderGroup &shaderGroup, const union rmcShaderData *data, const class Matrix34 *mtx, int bucket, int lod) const
{
    rmcModel *m = GetModel(lod);
    if (m) m->DrawSkinned(shaderGroup, data, mtx, 512, bucket, lod, nullptr);
}
