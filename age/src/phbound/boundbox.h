#ifndef PHBOUND_BOUNDBOX_H
#define PHBOUND_BOUNDBOX_H

#include "phbound/bound.h"

class phBoundBox : public phBound {
public:
    Vector3 Size;

    phBoundBox();
    phBoundBox(const Vector3 &size);
    virtual ~phBoundBox() {}

    virtual int GetType() const override { return BOX; }
    void SetSize(const Vector3 &size);
    const Vector3 &GetSize() const { return Size; }

    virtual Vector3 GetBoxMin() const override { return Centroid - Size * 0.5f; }
    virtual Vector3 GetBoxMax() const override { return Centroid + Size * 0.5f; }

    void CalculateExtents();
};

#endif // PHBOUND_BOUNDBOX_H
