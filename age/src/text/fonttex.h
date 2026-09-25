#ifndef TEXT_FONTTEX_H
#define TEXT_FONTTEX_H

#include "gfx/font.h"
#include "gfx/misc.h"
#include "core/types.h"

#include "data/unicode.h"   // _TCHAR / _T(): AGE text is wide
#include "atl/wstring.h"    // USES_CONVERSION / A2W / W2A for callers that format text

// Textured font: glyph metrics from <name>.fonttex plus 128x128 atlas pages
// in <name>_NN.tex.  (<name>.fontproj is the editor's source project and the
// _NNs.tex files are pre-baked shadow variants; neither is needed at runtime.)
//
// .fonttex layout (little-endian):
//   float version (1.02)
//   u32 height          line height in pixels
//   u32 spacing         extra pixels between glyphs
//   u32 unknown         14 for the PS2-era fonts, 6-7 for arial12/tiny
//   u32 spaceChar       always 0x20
//   u32 spaceWidth      advance for spaceChar / missing glyphs
//   u32 reserved[5]
//   u32 numGlyphs
//   numGlyphs * { u32 unicode; u8 x, y, w, h; s16 yOffset }   sorted by unicode
//   u32 numPages
//   numPages * u32      cumulative glyph end index per page
//
// A glyph's top edge is at lineTop + height + yOffset (yOffset is negative;
// descenders overshoot the line box by a few pixels).
//
// Drawing honours the txtCursor (text/cursor.h): pen offset, box, colour,
// scale, justification, wrapping, monospace digits and line-mode clipping;
// every Draw/ComputeExtents writes the laid-out size back into the cursor's
// mExtentX/mExtentY.
class txtFontTex : public gfxFont {
public:
    struct Glyph {
        u32 Char;
        u8 X, Y, W, H;
        s8 YOffset;
        s8 XOffset;
        u8 Page;
    };

    txtFontTex();
    ~txtFontTex();

    bool Load(const char* name);
    const char* GetName() const { return m_Name; }
    int GetHeight() const { return m_Height; }
    int GetSpacing() const { return m_Spacing; }
    int GetSpaceWidth() const { return m_SpaceWidth; }
    // Advance used for every glyph when the cursor is in monospace mode
    // (the widest digit, so clocks and counters keep their columns).
    int GetMonospaceAdvance() const { return m_MonoAdvance; }
    int GetNumPages() const { return m_NumPages; }

    void Draw(int x, int y, const wchar_t* text, class txtCursor* cursor) override;
    void Draw(int x, int y, const char* text, class txtCursor* cursor) override;
    using gfxFont::Draw;
    void ComputeExtents(int* w, int* h, const wchar_t* text) override;
    void ComputeExtents(int* w, int* h, const char* text) override;

    // AGE 2.72 forms.  Scale multiplies the cursor's scale; when `buffer` is
    // given the glyph quads are recorded into it (relative to x,y) instead
    // of drawn, so the caller can replay the layout (shadows, motion blur).
    // `dontDraw` runs the layout only (the cursor still receives the extents).
    void Draw(float x, float y, const wchar_t* text, class txtCursor* cursor, bool dontDraw = false, class txtDrawBuffer* buffer = 0);
    void Draw(float x, float y, const wchar_t* text, float scaleX, float scaleY, class txtCursor* cursor, class txtDrawBuffer* buffer = 0);
    void Draw3D(const class Vector3 &pos, const wchar_t* text, bool dontDraw, float scaleX, float scaleY, class txtCursor* cursor) {
        Draw(0.0f, 0.0f, text, scaleX, scaleY, cursor);
    }
    void ComputeExtents(int* w, int* h, const wchar_t* text, float scaleX, float scaleY, class txtCursor* cursor);
    void ComputeExtents(int* w, int* h, const wchar_t* text, class txtCursor* cursor) { ComputeExtents(w, h, text, 1.0f, 1.0f, cursor); }
    void SetBilinear(bool bilinear) { m_Bilinear = bilinear; }
    bool GetBilinear() const { return m_Bilinear; }

    // Debug: lists every cached font with its page textures' reference counts
    // (the game calls it around layer transitions to spot leaked fonts).
    static void PrintFontRefs();

private:
public:
    // Missing glyphs: ignore (draw nothing) instead of warning.
    static void SetMissingModeIgnore(bool ignore = true) { sm_MissingModeIgnore = ignore; }
    static bool sm_MissingModeIgnore;
    const Glyph* FindGlyph(u32 ch) const;
    int Advance(u32 ch, int monoAdvance = 0) const;
    int LineWidth(const wchar_t* text, int count, int monoAdvance = 0) const;
    int BreakLine(const wchar_t* text, int maxWidth, int monoAdvance = 0) const;
    void DrawLine(float x, float y, const wchar_t* text, int count, u32 color, float scaleX = 1.0f, float scaleY = 1.0f, class txtDrawBuffer* buffer = 0, float originX = 0.0f, float originY = 0.0f, int monoAdvance = 0);
    void DrawInternal(float x, float y, const wchar_t* text, class txtCursor* cursor, float scaleX, float scaleY, class txtDrawBuffer* buffer, bool dontDraw);

    char m_Name[32];
    int m_Height;
    int m_Spacing;
    int m_Unknown;
    u32 m_SpaceChar;
    int m_SpaceWidth;
    int m_MonoAdvance;
    Glyph* m_Glyphs;
    int m_NumGlyphs;
    class gfxTexture** m_Pages;
    int m_NumPages;
    bool m_Bilinear;
};

txtFontTex* txtGetFontTex(const char* name);
void txtFreeFontTex(txtFontTex* font);

inline wchar_t* A2WHelper(wchar_t* dest, const char* src, int maxLen)
{
    int i = 0;
    while (i < maxLen - 1 && src[i] != '\0') {
        dest[i] = (wchar_t)src[i];
        i++;
    }
    dest[i] = L'\0';
    return dest;
}

inline u8 redOf(gfxPackedColor c) { return (u8)((c >> 16) & 0xFF); }
inline u8 greenOf(gfxPackedColor c) { return (u8)((c >> 8) & 0xFF); }
inline u8 blueOf(gfxPackedColor c) { return (u8)(c & 0xFF); }
inline u8 alphaOf(gfxPackedColor c) { return (u8)((c >> 24) & 0xFF); }

// Bakes the string into a fresh power-of-two RGBA texture (transparent
// background) using the font's glyph pages.  Caller owns the returned
// reference (gfxFreeTexture / Release).  Never returns null once the gfx
// device is up -- ui code divides by the texture's dimensions unguarded.
class gfxTexture* RenderStringIntoTexture(const wchar_t* string, class gfxFont* font, int width, int height);

#endif // TEXT_FONTTEX_H
