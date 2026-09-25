#ifndef TEXT_DRAWBUFFER_H
#define TEXT_DRAWBUFFER_H

////////////////////////////////////////
// text/drawbuffer.h
//
// txtDrawBuffer - a recorded string layout.  txtFontTex::Draw(..., &buffer)
// stores the glyph quads (page texture + screen rectangle + texel rectangle)
// relative to the draw origin instead of rendering them; Draw() then replays
// the layout any number of times at different offsets / colours (motion blur
// trails, shadows) without re-running the layout.
////////////////////////////////////////

#include "core/types.h"
#include <vector>

class gfxTexture;

class txtDrawBuffer {
public:
    struct Quad {
        gfxTexture *mPage;
        float mX, mY, mW, mH;        // screen rectangle relative to the origin
        float mU0, mV0, mU1, mV1;    // texel rectangle in the page
    };

    txtDrawBuffer();
    ~txtDrawBuffer();

    void Clear();
    void Add(gfxTexture *page, float x, float y, float w, float h, float u0, float v0, float u1, float v1);
    int GetNumQuads() const { return (int)mQuads.size(); }
    const Quad &GetQuad(int i) const { return mQuads[i]; }

    // Bounds of the recorded layout (relative to the origin).
    float GetWidth() const { return mMaxX - mMinX; }
    float GetHeight() const { return mMaxY - mMinY; }
    float GetMinX() const { return mMinX; }
    float GetMinY() const { return mMinY; }

    // Replays the layout with its origin at (x, y), modulated by color.
    void Draw(float x, float y, u32 color) const;

private:
    std::vector<Quad> mQuads;
    float mMinX, mMinY, mMaxX, mMaxY;
};

#endif // TEXT_DRAWBUFFER_H
