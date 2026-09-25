#include "rmcore/cpvpalette.h"
#include "data/assetcfg.h"
#include "data/token.h"
#include "data/rscimage.h"

const rmcCpvPalette *rmcCpvPalette::sm_Current = nullptr;
bool rmcCpvPalette::sm_Dirty = false;
bool rmcCpvPalette::sm_DownloadSwizzled = false;

static rmcCpvPalette s_DefaultPalette;

const rmcCpvPalette &rmcCpvPalette::GetCurrent() {
    return sm_Current ? *sm_Current : s_DefaultPalette;
}

rmcCpvPalette::rmcCpvPalette() {
    int i = 0;
    // Standard 6x6x6 RGB cube, fully opaque
    for (int r = 0; r < 6; r++) {
        for (int g = 0; g < 6; g++) {
            for (int b = 0; b < 6; b++) {
                m_Colors[i++].Set(r / 5.0f, g / 5.0f, b / 5.0f, 1.0f);
            }
        }
    }
    // Grayscale / transparency ramp for remaining entries
    for (; i < 256; i++) {
        float j = (i - 216) / 39.0f;
        m_Colors[i].Set(1.0f, 1.0f, 1.0f, j);
    }
}

bool rmcCpvPalette::Load(const char *name) {
    Stream *S = ASSET.Open(name, "txt");
    if (!S) return false;
    datTokenizer T;
    T.Init(name, S);
    for (int i = 0; i < 256; i++) {
        T.GetVector(m_Colors[i]);
    }
    m_Colors[255].Set(1.0f, 1.0f, 1.0f, 1.0f);
    S->Close();
    return true;
}

bool rmcCpvPalette::InitFromImage(const datResourceImage &image, u32 paletteAddr) {
    if (!paletteAddr || !image.IsValidAddress(paletteAddr, 256 * 16)) return false;
    for (int i = 0; i < 256; i++) {
        u32 p = paletteAddr + (u32)i * 16;
        m_Colors[i].Set(image.ReadFloat(p), image.ReadFloat(p + 4), image.ReadFloat(p + 8), image.ReadFloat(p + 12));
    }
    return true;
}

static inline float colordist(const Vector4 &a, const Vector4 &b) {
    float dR = a.x - b.x;
    float dG = a.y - b.y;
    float dB = a.z - b.z;
    float dA = a.w - b.w;
    return dR * dR + dG * dG + dB * dB + dA * dA;
}

u8 rmcCpvPalette::Lookup(const Vector4 &color) const {
    float bestDist = colordist(color, m_Colors[255]);
    if (bestDist == 0.0f) return 255;
    u8 bestIdx = 255;
    for (int i = 0; i < 255; i++) {
        float thisDist = colordist(color, m_Colors[i]);
        if (thisDist < bestDist) {
            bestDist = thisDist;
            bestIdx = (u8)i;
        }
    }
    return bestIdx;
}

void rmcCpvPalette::Download() const {
    // No-op on PC
}
