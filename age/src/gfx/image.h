#ifndef GFX_IMAGE_H
#define GFX_IMAGE_H

// A decoded RGBA8 image (gfxLoadTargaImage, PIPE.CreateReadbackImage):
// loading screens, frontend pages, map-tile snapshots.
class gfxImage {
public:
    enum Format { gf8888 };
    void* m_RGBA;      // Width*Height*4, top-down
    int Width;
    int Height;
    gfxImage() : m_RGBA(0), Width(0), Height(0) {}
    void Release();    // frees the pixels and this
    int GetWidth() const { return Width; }
    int GetHeight() const { return Height; }
    // Resample to w x h (nearest neighbour) in place.
    void Scale(int w, int h);

    // New image holding the (x,y,w,h) window of this one, clamped to the
    // bounds (pixels outside are black/opaque).  Caller Release()s it.
    gfxImage *CreateSubImage(int x, int y, int w, int h) const;
};

#endif // GFX_IMAGE_H
