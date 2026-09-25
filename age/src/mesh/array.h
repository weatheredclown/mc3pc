#ifndef MESH_ARRAY_H
#define MESH_ARRAY_H

#include "atl/array.h"

template <typename T>
class mshArray : public atArray<T> {
public:
    void Append3(const T &a, const T &b, const T &c) {
        this->Append(a);
        this->Append(b);
        this->Append(c);
    }
    void Assume(mshArray<T> &other) {
        this->Resize(other.GetCount());
        for (int i = 0; i < other.GetCount(); i++) {
            (*this)[i] = other[i];
        }
        other.Reset();
    }
};

#endif // MESH_ARRAY_H
