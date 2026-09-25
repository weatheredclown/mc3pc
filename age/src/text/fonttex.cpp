#include "core/output.h"
#include "text/fonttex.h"
#include "text/drawbuffer.h"
#include "text/cursor.h"
#include "core/stream.h"
#include "data/assetcfg.h"
#include "data/hash.h"
#include "gfx/rstate.h"
#include "gfx/simple.h"
#include "gfx/texture.h"
#include <stdio.h>
#include <string.h>

txtFontTex::txtFontTex() {
  m_Bilinear = false;
  m_Name[0] = '\0';
  m_Height = 0;
  m_Spacing = 0;
  m_Unknown = 0;
  m_SpaceChar = 0x20;
  m_SpaceWidth = 0;
  m_Glyphs = nullptr;
  m_NumGlyphs = 0;
  m_Pages = nullptr;
  m_NumPages = 0;
}

txtFontTex::~txtFontTex() {
  delete[] m_Glyphs;
  if (m_Pages) {
    for (int i = 0; i < m_NumPages; i++) {
      if (m_Pages[i])
        m_Pages[i]->Release();
    }
    delete[] m_Pages;
  }
}

bool txtFontTex::Load(const char *name) {
  strncpy(m_Name, name, sizeof(m_Name) - 1);
  m_Name[sizeof(m_Name) - 1] = '\0';

  // Root-relative: PushFolder nests, and fonts are requested from inside pushed
  // tune folders.  The disc keeps the glyph pages under fonts/texture/.
  SafeStream s = ASSET.Open("$/fonts", name, "fonttex");
  if (!s) {
    printf("[FONTTEX] ERROR: Failed to open font '%s'\n", name);
    fflush(stdout);
    return false;
  }

  float version = 0.0f;
  s->Read(&version, 4);
  if (version < 1.0f || version > 1.1f) {
    printf("[FONTTEX] ERROR: '%s' has unexpected version %f\n", name, version);
    fflush(stdout);
    return false;
  }

  u32 hdr[10] = {};
  s->Read(hdr, sizeof(hdr));
  m_Height = (int)hdr[0];
  m_Spacing = (int)hdr[1];
  m_Unknown = (int)hdr[2];
  m_SpaceChar = hdr[3];
  m_SpaceWidth = (int)hdr[4];
  // hdr[5..9] reserved/unknown small ints

  u32 numGlyphs = 0;
  s->Read(&numGlyphs, 4);
  if (numGlyphs == 0 || numGlyphs > 4096) {
    printf("[FONTTEX] ERROR: '%s' glyph count %u fails sanity check\n", name,
           numGlyphs);
    fflush(stdout);
    return false;
  }

  m_NumGlyphs = (int)numGlyphs;
  m_Glyphs = new Glyph[m_NumGlyphs];
  for (int i = 0; i < m_NumGlyphs; i++) {
    Glyph &g = m_Glyphs[i];
    s->Read(&g.Char, 4);
    s->Read(&g.X, 1);
    s->Read(&g.Y, 1);
    s->Read(&g.W, 1);
    s->Read(&g.H, 1);
    s->Read(&g.YOffset, 1);
    s->Read(&g.XOffset, 1);
    g.Page = 0;
  }

  u32 numPages = 0;
  s->Read(&numPages, 4);
  if (numPages == 0 || numPages > 64) {
    printf("[FONTTEX] ERROR: '%s' page count %u fails sanity check\n", name,
           numPages);
    fflush(stdout);
    return false;
  }

  m_NumPages = (int)numPages;
  m_Pages = new gfxTexture *[m_NumPages];
  int glyphStart = 0;
  for (int p = 0; p < m_NumPages; p++) {
    u32 glyphEnd = 0;
    s->Read(&glyphEnd, 4);
    for (int i = glyphStart; i < (int)glyphEnd && i < m_NumGlyphs; i++) {
      m_Glyphs[i].Page = (u8)p;
    }
    glyphStart = (int)glyphEnd;

    char pageName[64];
    snprintf(pageName, sizeof(pageName), "$/fonts/texture/%s_%02d", name, p);
    m_Pages[p] = gfxGetTexture(pageName, false);
    if (!m_Pages[p]) {
      printf("[FONTTEX] WARNING: '%s' missing page texture '%s'\n", name,
             pageName);
      fflush(stdout);
    }
  }

  // Let base-class users (extent estimates) see sensible cell metrics.
  m_CellHeight = m_Height;
  m_CellWidth = m_SpaceWidth;

  printf("[FONTTEX] Loaded '%s': height=%d spacing=%d space=%d glyphs=%d "
         "pages=%d\n",
         name, m_Height, m_Spacing, m_SpaceWidth, m_NumGlyphs, m_NumPages);
  fflush(stdout);
  return true;
}

const txtFontTex::Glyph *txtFontTex::FindGlyph(u32 ch) const {
  int lo = 0, hi = m_NumGlyphs - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    if (m_Glyphs[mid].Char == ch)
      return &m_Glyphs[mid];
    if (m_Glyphs[mid].Char < ch)
      lo = mid + 1;
    else
      hi = mid - 1;
  }
  // Case fallback for fonts that only carry one case (e.g. caps-only fonts).
  if (ch >= 'a' && ch <= 'z')
    return FindGlyph(ch - 'a' + 'A');
  return nullptr;
}

int txtFontTex::Advance(u32 ch, int monoAdvance) const {
  if (monoAdvance > 0)
    return monoAdvance;
  if (ch == m_SpaceChar)
    return m_SpaceWidth;
  const Glyph *g = FindGlyph(ch);
  return g ? g->W + m_Spacing : m_SpaceWidth;
}

int txtFontTex::LineWidth(const wchar_t *text, int count, int monoAdvance) const {
  int w = 0;
  for (int i = 0; i < count; i++)
    w += Advance((u32)text[i], monoAdvance);
  return w;
}

// Number of chars of `text` that fit in maxWidth, breaking at spaces where
// possible.  Always consumes at least one char.
int txtFontTex::BreakLine(const wchar_t *text, int maxWidth, int monoAdvance) const {
  int w = 0, lastSpace = -1;
  int i = 0;
  for (; text[i] && text[i] != L'\n'; i++) {
    if (text[i] == (wchar_t)m_SpaceChar)
      lastSpace = i;
    w += Advance((u32)text[i], monoAdvance);
    if (w > maxWidth && i > 0)
      return lastSpace > 0 ? lastSpace : i;
  }
  return i;
}

void txtFontTex::DrawLine(float x, float y, const wchar_t *text, int count,
                          u32 color, float scaleX, float scaleY,
                          txtDrawBuffer *buffer, float originX, float originY, int monoAdvance) {
  static int s_lineLog = 0;
  if (s_lineLog++ < 10) {
    Displayf("[DRAWLINE #%d] (x,y)=(%.1f,%.1f) count=%d color=0x%08X scale=(%.2f,%.2f)", s_lineLog, x, y, count, color, scaleX, scaleY);
  }
  float pen = x;
  for (int i = 0; i < count; i++) {
    u32 ch = (u32)text[i];
    float adv = (float)Advance(ch, monoAdvance) * scaleX;
    if (ch == m_SpaceChar) {
      pen += adv;
      continue;
    }
    const Glyph *g = FindGlyph(ch);
    if (!g) {
      pen += adv;
      continue;
    }
    gfxTexture *page = (g->Page < m_NumPages) ? m_Pages[g->Page] : nullptr;
    if (page) {
      float gx = pen;
      float gy = y + ((float)m_Height + (float)g->YOffset) * scaleY;
      float gw = (float)g->W * scaleX;
      float gh = (float)g->H * scaleY;
      static int s_glyphLog = 0;
      if (s_glyphLog++ < 10) {
        Displayf("[GLYPH #%d] ch=%u ('%c') y=%.1f m_Height=%d YOffset=%d XOffset=%d gy=%.1f scaleY=%.2f",
            s_glyphLog, g->Char, (char)g->Char, y, m_Height, (int)g->YOffset, (int)g->XOffset, gy, scaleY);
      }
      if (buffer) {
        buffer->Add(page, gx - originX, gy - originY, gw, gh, (float)g->X, (float)g->Y,
                    (float)g->X + g->W, (float)g->Y + g->H);
      } else {
        RSTATE.SetTexture(page);
        PIPE.Blit2D(gx, gy, gx + gw, gy + gh, (float)g->X, (float)g->Y, (float)g->X + g->W,
                    (float)g->Y + g->H, color);
      }
    }
    pen += adv;
  }
}

void txtFontTex::DrawInternal(float x, float y, const wchar_t *text, txtCursor *cursor,
                              float scaleX, float scaleY, txtDrawBuffer *buffer, bool dontDraw) {
  static int s_drawIntLog = 0;
  if (s_drawIntLog++ < 15) {
    Displayf("[DRAWINTERNAL #%d] font='%s' glyphs=%d pages=%p text=%p ('%ls') (x,y)=(%.1f,%.1f)",
        s_drawIntLog, m_Name, m_NumGlyphs, m_Pages, text, text ? text : L"null", x, y);
  }
  if (!text || !*text)
    return;
  if (!m_NumGlyphs || !m_Pages) {
    if (!buffer) gfxFont::Draw((int)x, (int)y, text, cursor);
    return;
  }

  u32 color = 0xFFFFFFFF;
  bool centered = false, wrapped = false;
  int boxWidth = 0;
  int mono = 0;
  if (cursor) {
    mono = cursor->mMonospace ? GetMonospaceAdvance() : 0;
    color = cursor->mColor;
    centered = cursor->mCentered;
    wrapped = cursor->mWrapped;
    boxWidth = cursor->mRight - cursor->mLeft;
    scaleX *= cursor->GetScaleX();
    scaleY *= cursor->GetScaleY();
  }
  if (scaleX <= 0.0f) scaleX = 1.0f;
  if (scaleY <= 0.0f) scaleY = 1.0f;

  float lineY = y;
  const wchar_t *p = text;
  while (*p) {
    int count;
    if (wrapped && boxWidth > 0)
      count = BreakLine(p, (int)(boxWidth / scaleX), mono);
    else {
      count = 0;
      while (p[count] && p[count] != L'\n')
        count++;
    }

    float lineX = x;
    if (centered && boxWidth > 0)
      lineX += ((float)boxWidth - (float)LineWidth(p, count, mono) * scaleX) * 0.5f;
    if (!dontDraw) DrawLine(lineX, lineY, p, count, color, scaleX, scaleY, buffer, x, y, mono);

    p += count;
    while (*p == (wchar_t)m_SpaceChar)
      p++; // swallow the break point
    if (*p == L'\n')
      p++;
    lineY += (float)m_Height * scaleY;
  }
}

void txtFontTex::Draw(int x, int y, const wchar_t *text, txtCursor *cursor) {
  DrawInternal((float)x, (float)y, text, cursor, 1.0f, 1.0f, 0, false);
}

void txtFontTex::Draw(float x, float y, const wchar_t *text, txtCursor *cursor, bool,
                      txtDrawBuffer *buffer) {
  DrawInternal(x, y, text, cursor, 1.0f, 1.0f, buffer, false);
}

void txtFontTex::Draw(float x, float y, const wchar_t *text, float scaleX, float scaleY,
                      txtCursor *cursor, txtDrawBuffer *buffer) {
  DrawInternal(x, y, text, cursor, scaleX, scaleY, buffer, false);
}

void txtFontTex::ComputeExtents(int *w, int *h, const wchar_t *text, float scaleX,
                                float scaleY, txtCursor *cursor) {
  int bw = 0, bh = 0;
  ComputeExtents(&bw, &bh, text);
  if (cursor) {
    scaleX *= cursor->GetScaleX();
    scaleY *= cursor->GetScaleY();
  }
  if (w) *w = (int)(bw * scaleX);
  if (h) *h = (int)(bh * scaleY);
}

void txtFontTex::Draw(int x, int y, const char *text, txtCursor *cursor) {
  if (!text)
    return;
  // PC port: direct fallback to base stroke font without round-tripping to wide char
  if (!m_NumGlyphs || !m_Pages) {
    gfxFont::Draw(x, y, text, cursor);
    return;
  }
  wchar_t buf[512];
  A2WHelper(buf, text, 512);
  Draw(x, y, buf, cursor);
}

void txtFontTex::ComputeExtents(int *w, int *h, const wchar_t *text) {
  if (!text)
    return;
  if (!m_NumGlyphs) {
    gfxFont::ComputeExtents(w, h, text);
    return;
  }
  int maxWidth = 0, lines = 1, lineWidth = 0;
  for (const wchar_t *p = text; *p; p++) {
    if (*p == L'\n') {
      lines++;
      lineWidth = 0;
      continue;
    }
    lineWidth += Advance((u32)*p);
    if (lineWidth > maxWidth)
      maxWidth = lineWidth;
  }
  if (w)
    *w = maxWidth;
  if (h)
    *h = lines * m_Height;
}

void txtFontTex::ComputeExtents(int *w, int *h, const char *text) {
  if (!text)
    return;
  wchar_t buf[512];
  A2WHelper(buf, text, 512);
  ComputeExtents(w, h, buf);
}

gfxTexture *RenderStringIntoTexture(const wchar_t *string, gfxFont *font,
                                    int width, int height) {
  if (width < 1)
    width = 1;
  if (height < 1)
    height = 1;
  int tw = 1, th = 1;
  while (tw < width + 1)
    tw <<= 1;
  while (th < height + 1)
    th <<= 1;
  gfxTexture *tex = gfxCreateTextTarget(tw, th);
  if (!tex)
    return NULL;
  gfxBeginRenderToTexture(tex);
  // Straight write into the cleared-transparent target: the glyph texels
  // (color and alpha) land in the texture untouched, so blending happens once,
  // when the baked texture is drawn.
  RSTATE.SetAlphaBlendEnable(false);
  if (string && font)
    font->Draw(0, 0, string, nullptr);
  RSTATE.SetAlphaBlendEnable(true);
  gfxEndRenderToTexture();
  return tex;
}

static HashTable sFontCache;

txtFontTex *txtGetFontTex(const char *name) {
  if (!name || !*name)
    return nullptr;
  txtFontTex *font = (txtFontTex *)sFontCache.Find(name);
  if (font)
    return font;
  font = new txtFontTex();
  font->Load(name); // a failed load still caches: Draw falls back to SYSFONT
  sFontCache.Add(name, font);
  return font;
}

void txtFreeFontTex(txtFontTex *font) {
  // Fonts are cached for the process lifetime.
}

// gfx/font.h: the game calls these around heap begin/end so the default font
// is allocated early (not on first draw).  Fonts here are created on demand
// by txtGetFontTex, so bring-up only needs to exist.
static bool s_FontInitialized = false;
void gfxInitFont() { s_FontInitialized = true; }
void gfxKillFont() { s_FontInitialized = false; }

bool txtFontTex::sm_MissingModeIgnore = false;

// Debug listing of the font cache (the game calls it around layer changes).
void txtFontTex::PrintFontRefs() {
  Displayf("[fonts] %d cached", sFontCache.GetNumEntries());
  for (int i = 0; i < sFontCache.GetNumEntries(); i++) {
    const char *name = sFontCache.AccessName(i);
    txtFontTex *font = (txtFontTex *)sFontCache.AccessData(i);
    Displayf("[fonts]   %s %s", name ? name : "?", font ? "" : "(failed load)");
  }
}
