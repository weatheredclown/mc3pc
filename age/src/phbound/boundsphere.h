#ifndef PHBOUND_BOUNDSPHERE_H
#define PHBOUND_BOUNDSPHERE_H

#include "phbound/bound.h"

class phBoundSphere : public phBound {
public:
    float Radius;
    phBoundSphere();
    phBoundSphere(float radius);
    float GetRadius() const;
    virtual int GetType() const override { return SPHERE; }
    virtual void Draw(const Matrix34 &transform) const override;
    virtual float GetActualRadius() const override;
};

#endif // PHBOUND_BOUNDSPHERE_H
