#ifndef GFX_FONT_H
#define GFX_FONT_H

#include "gfx/vgl.h"
#include "core/types.h"

extern "C" void gfxDrawFont(int x, int y, const char *text,
                            unsigned int color = 0xFFFFFFFF);
// Same stroke font at an explicit scale (gfxDrawFont uses 1.4).
extern "C" void gfxDrawFontScaled(int x, int y, const char *text,
                                  unsigned int color, float scale);

// Metrics of the built-in stroke font (rgl.cpp gfxDrawFont: 6-unit advance,
// 7-unit tall glyphs, drawn at 1.4x).
inline int gfxFontGetWidth() { return 8; }
inline int gfxFontGetHeight() { return 10; }

// Screen-space connected line strip (xy = x0,y0,x1,y1,...), for debug graphs.
extern "C" void gfxDrawPolyline2D(const float *xy, int numPoints,
                                  unsigned int color);

class gfxFont {
public:
  virtual ~gfxFont() {}
  void SetBilinear(bool b = true) { m_Bilinear = b; }
  bool GetBilinear() const { return m_Bilinear; }
  bool m_Bilinear = true;
  int m_CellHeight = 8;
  int m_CellWidth = 6;
  virtual void Draw(int x, int y, const wchar_t *text, class txtCursor *cursor) {
    if (!text) return;
    char buf[512];
    int i = 0;
    while (i < 511 && text[i] != L'\0') {
      buf[i] = (char)text[i];
      i++;
    }
    buf[i] = '\0';
    gfxFont::Draw(x, y, buf, cursor); // PC port: explicitly qualify to prevent infinite virtual recursion
  }
  virtual void Draw(int x, int y, const char *text, class txtCursor *cursor) {
    gfxDrawFont(x, y, text);
  }
  void Draw(int x, int y, const wchar_t *text) { Draw(x, y, text, nullptr); }
  void Draw(int x, int y, const char *text) { Draw(x, y, text, nullptr); }
  virtual void ComputeExtents(int *w, int *h, const wchar_t *text) {
    if (!text) return;
    int len = 0;
    while (text[len] != L'\0') len++;
    if (w)
      *w = len * m_CellWidth;
    if (h)
      *h = m_CellHeight;
  }
  virtual void ComputeExtents(int *w, int *h, const char *text) {
    if (!text) return;
    int len = 0;
    while (text[len] != '\0') len++;
    if (w)
      *w = len * m_CellWidth;
    if (h)
      *h = m_CellHeight;
  }
};


extern gfxFont *SYSFONT_ptr;
#define SYSFONT (*SYSFONT_ptr)

// Font subsystem bring-up (the default font is created lazily by the text layer).
void gfxInitFont();
void gfxKillFont();

#endif // GFX_FONT_H
