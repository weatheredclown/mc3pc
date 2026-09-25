#ifndef GFX_TEXPAL_H
#define GFX_TEXPAL_H

class gfxImageRGBA {
public:
    u8 r, g, b, a;
    void Set(int _r, int _g, int _b, int _a) { r = _r; g = _g; b = _b; a = _a; }
};

// CPU-side CLUT of an 8-bit paletted texture.  Effects (flash/strike hit FX)
// recolour themselves by rewriting the entries; Update() re-expands the
// texture's index map through the palette and re-uploads it.
class gfxTexturePalette {
public:
    static gfxTexturePalette* Create() { return new gfxTexturePalette(); }
    gfxTexturePalette() : Owner(nullptr) { for (int i = 0; i < 256; i++) Entries[i].Set(255, 255, 255, 255); }
    void Release() { delete this; }
    // PS2: upload the CLUT to GS memory at `addr`.  PC: the CLUT lives with
    // the texture, so a download re-expands the owner through it (Update).
    void Download(int addr) { m_DownloadAddr = addr; Update(); }
    int GetDownloadAddr() const { return m_DownloadAddr; }
    void SetEntry(int index, const gfxImageRGBA &rgba) { if (index >= 0 && index < 256) Entries[index] = rgba; }
    const gfxImageRGBA &GetEntry(int index) const { return Entries[index & 255]; }
    void Update();
    class gfxTexture *Owner;
private:
    gfxImageRGBA Entries[256];
    int m_DownloadAddr = 0;
};

#endif // GFX_TEXPAL_H
