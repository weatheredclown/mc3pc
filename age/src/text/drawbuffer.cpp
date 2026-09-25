#include "text/drawbuffer.h"

#include "gfx/simple.h"
#include "gfx/vgl.h"
#include "gfx/rstate.h"
#include "gfx/texture.h"

txtDrawBuffer::txtDrawBuffer()
    : mMinX(0.0f), mMinY(0.0f), mMaxX(0.0f), mMaxY(0.0f)
{
}

txtDrawBuffer::~txtDrawBuffer()
{
}

void txtDrawBuffer::Clear()
{
    mQuads.clear();
    mMinX = mMinY = mMaxX = mMaxY = 0.0f;
}

void txtDrawBuffer::Add(gfxTexture *page, float x, float y, float w, float h, float u0, float v0, float u1, float v1)
{
    Quad q;
    q.mPage = page;
    q.mX = x; q.mY = y; q.mW = w; q.mH = h;
    q.mU0 = u0; q.mV0 = v0; q.mU1 = u1; q.mV1 = v1;
    if (mQuads.empty()) {
        mMinX = x; mMinY = y; mMaxX = x + w; mMaxY = y + h;
    } else {
        if (x < mMinX) mMinX = x;
        if (y < mMinY) mMinY = y;
        if (x + w > mMaxX) mMaxX = x + w;
        if (y + h > mMaxY) mMaxY = y + h;
    }
    mQuads.push_back(q);
}

void txtDrawBuffer::Draw(float x, float y, u32 color) const
{
    gfxTexture *bound = 0;
    for (size_t i = 0; i < mQuads.size(); i++) {
        const Quad &q = mQuads[i];
        if (!q.mPage)
            continue;
        if (q.mPage != bound) {
            RSTATE.SetTexture(q.mPage);
            bound = q.mPage;
        }
        PIPE.Blit2D(x + q.mX, y + q.mY, x + q.mX + q.mW, y + q.mY + q.mH,
                    q.mU0, q.mV0, q.mU1, q.mV1, color);
    }
}
