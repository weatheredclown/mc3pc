////////////////////////////////////////
// randmath.h
////////////////////////////////////////

#ifndef VECTOR_RANDMATH_H
#define VECTOR_RANDMATH_H
#include "core/types.h"   // ClampRange

#include <stdlib.h>

inline int irand() {
    return rand();
}

inline int irand(int max) {
    return max > 0 ? rand() % max : 0;
}


inline float RangeRand(float min, float max) {
    float r = (float)rand() / (float)RAND_MAX;
    return min + r * (max - min);
}

inline float frand() {
    return (float)rand() / (float)RAND_MAX;
}

inline float frand(float max) {
    return frand() * max;
}

inline float frand(int max) {
    return frand() * (float)max;
}

inline float vrand(float v) {
    return (frand() * 2.0f - 1.0f) * v;
}

inline void ResetRandomSeed() {
    srand(0);
}

#endif // VECTOR_RANDMATH_H
