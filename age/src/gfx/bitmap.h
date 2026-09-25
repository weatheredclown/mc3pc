#ifndef GFX_BITMAP_H
#define GFX_BITMAP_H

#include "gfx/image.h"

// A drawable image (PIPE.CopyBitmap): backed by a texture on the PC port.
class gfxBitmap {
public:
    class gfxTexture *Texture;
    int Width;
    int Height;
    gfxBitmap() : Texture(0), Width(0), Height(0) {}
    static gfxBitmap* Create(gfxImage* img);   // texture.cpp
    int GetWidth() const { return Width; }
    int GetHeight() const { return Height; }
    void Release();   // texture.cpp: frees the texture and this
};

void gfxFreeBitmap(gfxBitmap* bmp);            // texture.cpp

// The loading screen has to stay up for the whole load, but the loading pump
// (mcLoadingThread::UpdateProgress) presents a frame per iteration and only
// clears the strip under the "now loading" logo.  The PS2 could do that
// because its framebuffer persisted; a D3D11 swap chain hands back an
// undefined buffer after every Present, so the rest of the screen went black.
// Hold the decoded screen here and re-blit it at the top of each pumped frame.
void gfxSetLoadingBackdrop(gfxBitmap* bmp);    // takes ownership; NULL releases
void gfxDrawLoadingBackdrop();                 // centred blit, no-op when unset

#endif // GFX_BITMAP_H
