#ifndef DATA_RESOURCEHELPERS_H
#define DATA_RESOURCEHELPERS_H

#include <new>
#ifndef BUILD_SYSTEM_VERSION
#define BUILD_SYSTEM_VERSION 1
#endif

#include "data/resource.h"

template <typename T>
inline T* VirtualConstructFromPtr(void *ptr) {
    return (T*)ptr;
}

// Console: `ptr` already points at the object's memory inside the paged-in
// block; fix the pointer up and run the page-in constructor in place.
// PC: `ptr` carries the image address left by rsc.PointerFixup; the object is
// constructed on the heap from its place in the image (see data/resource.h).
template <typename T>
inline void VirtualConstructFromPtr(datResource &rsc, T *&ptr) {
    if (ptr) {
        rsc.Construct(ptr);
    }
}

#endif // DATA_RESOURCEHELPERS_H
