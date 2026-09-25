#ifndef PHBOUND_BOUNDHOTDOG_H
#define PHBOUND_BOUNDHOTDOG_H

#include "phbound/bound.h"

class phBoundHotdog : public phBound {
public:
    float Radius;
    float Length;

    phBoundHotdog();
    phBoundHotdog(float radius, float length);

    float GetLength() const;
    float GetRadius() const;
    void SetSize(float radius, float length);
    void SetLength(float length) { Length = length; }
    void SetRadius(float radius) { Radius = radius; }
    virtual int GetType() const override { return HOTDOG; }
    virtual void Draw(const Matrix34 &transform) const override;
    virtual float GetActualRadius() const override;
};

#endif // PHBOUND_BOUNDHOTDOG_H
