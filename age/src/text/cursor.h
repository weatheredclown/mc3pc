#ifndef TEXT_CURSOR_H
#define TEXT_CURSOR_H

////////////////////////////////////////
// text/cursor.h
//
// txtCursor - layout box, pen offset, colour, scale and justification used by
// txtFontTex::Draw.  The box (mTop/mBottom/mLeft/mRight) confines wrapped
// text and is the reference for centred / right justified lines.
//
// After a Draw the cursor reports the pixel extents of what was laid out in
// mExtentX/mExtentY (the chat box sizes its lines that way).  Monospace
// mode advances every glyph by the widest digit so clocks and timers do not
// jitter; line mode clips whole lines to the box's top/bottom edges so a
// scrolling text can be drawn by moving the pen above the box.
////////////////////////////////////////

#include "core/types.h"
#include "data/unicode.h"    // _TCHAR for text callers that only include the cursor
#include "vector/Vector4.h"
#include "gfx/vgl.h"

class txtCursor {
public:
    enum Justify { kLeftJustify = 0, kCenterJustify, kRightJustify };

    int mTop;
    int mBottom;
    int mLeft;
    int mRight;
    int mX;              // pen offset added to the draw position
    int mY;
    u32 mColor;
    bool mCentered;      // kept for old callers; mirrors mJustify == kCenterJustify
    bool mWrapped;
    float mScaleX;       // multiplied into the scale passed to Draw
    float mScaleY;
    u32 mAttributes;     // free for the caller (mc3 zeroes it); the font ignores it
    int mJustify;
    bool mMonospace;     // every glyph advances by the widest digit
    bool mLineMode;      // lines outside [mTop, mBottom) are not drawn
    int mExtentX;        // pixel size of the last Draw / ComputeExtents (scaled)
    int mExtentY;

    txtCursor()
        : mTop(0), mBottom(0), mLeft(0), mRight(0),
          mX(0), mY(0), mColor(0xFFFFFFFF),
          mCentered(false), mWrapped(false),
          mScaleX(1.0f), mScaleY(1.0f), mAttributes(0), mJustify(kLeftJustify),
          mMonospace(false), mLineMode(false), mExtentX(0), mExtentY(0) {}

    void SetCentered(bool centered = true) { mCentered = centered; mJustify = centered ? kCenterJustify : kLeftJustify; }
    void SetLeftJustify() { mJustify = kLeftJustify; mCentered = false; }
    void SetCenterJustify() { mJustify = kCenterJustify; mCentered = true; }
    void SetRightJustify() { mJustify = kRightJustify; mCentered = false; }
    bool IsCentered() const { return mCentered || mJustify == kCenterJustify; }
    void SetWrapped(bool wrapped = true) { mWrapped = wrapped; }
    bool GetWrapped() const { return mWrapped; }
    void SetMonospace(bool monospace = true) { mMonospace = monospace; }
    bool GetMonospace() const { return mMonospace; }
    void SetLineMode(bool lineMode = true) { mLineMode = lineMode; }
    bool GetLineMode() const { return mLineMode; }
    void SetColor(u32 color) { mColor = color; }
    void SetColor(const Vector4 &color) {
        mColor = mkfrgba(color.x, color.y, color.z, color.w);
    }
    u32 GetColor() const { return mColor; }
    void SetScale(float scaleX, float scaleY) { mScaleX = scaleX; mScaleY = scaleY; }
    float GetScaleX() const { return mScaleX; }
    float GetScaleY() const { return mScaleY; }
    void Set(int top, int bottom, int left, int right) {
        mTop = top;
        mBottom = bottom;
        mLeft = left;
        mRight = right;
    }
    void SetCursor(int x, int y) {
        mX = x;
        mY = y;
    }
    int GetBoxWidth() const { return mRight - mLeft; }
    int GetBoxHeight() const { return mBottom - mTop; }
    int GetExtentX() const { return mExtentX; }
    int GetExtentY() const { return mExtentY; }
};

#endif // TEXT_CURSOR_H
