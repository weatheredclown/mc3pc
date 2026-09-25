#ifndef RMCORE_CPVPALETTE_H
#define RMCORE_CPVPALETTE_H

#include "core/types.h"
#include "vector/vector4.h"
#include "gfx/vgl.h"

class datResourceImage;

class rmcCpvPalette {
public:
    Vector4 m_Colors[256];

    static const rmcCpvPalette *sm_Current;
    static bool sm_Dirty;
    static bool sm_DownloadSwizzled;

    rmcCpvPalette();
    ~rmcCpvPalette() {}

    bool Load(const char *name);
    bool InitFromImage(const datResourceImage &image, u32 paletteAddr);

    u8 Lookup(const Vector4 &color) const;
    void Download() const;

    const Vector4 &GetEntry(int idx) const { return m_Colors[idx & 0xff]; }
    void SetEntry(int idx, const Vector4 &v) { m_Colors[idx & 0xff] = v; }
    rmcCpvPalette &operator=(const rmcCpvPalette &o) {
        for (int i = 0; i < 256; i++) m_Colors[i] = o.m_Colors[i];
        return *this;
    }

    const Vector4 &GetColor(int idx) const { return m_Colors[idx & 0xff]; }
    gfxPackedColor GetPackedColor(int idx) const {
        const Vector4 &c = m_Colors[idx & 0xff];
        float r = c.x < 0.0f ? 0.0f : (c.x > 1.0f ? 1.0f : c.x);
        float g = c.y < 0.0f ? 0.0f : (c.y > 1.0f ? 1.0f : c.y);
        float b = c.z < 0.0f ? 0.0f : (c.z > 1.0f ? 1.0f : c.z);
        float a = (c.w <= 0.0f) ? 1.0f : (c.w > 1.0f ? 1.0f : c.w);
        return mkfrgba(r, g, b, a);
    }

    static void SetCurrent(const rmcCpvPalette &pal) { sm_Current = &pal; }
    static void SetCurrentPtr(const rmcCpvPalette *pal) { sm_Current = pal; }
    static const rmcCpvPalette *GetCurrentPtr() { return sm_Current; }
    static const rmcCpvPalette &GetCurrent();
    static bool HasCurrent() { return sm_Current != nullptr; }
};

#endif // RMCORE_CPVPALETTE_H
