#ifndef VECTOR_SIMPLEMATH_H
#define VECTOR_SIMPLEMATH_H

#include <math.h>
#include "core/types.h"

#ifndef Sign
template <typename T>
inline T Sign(T val) { return (val > 0) ? (T)1 : ((val < 0) ? (T)-1 : (T)0); }
#endif

#ifndef Square
template <typename T>
inline T Square(T val) { return val * val; }
#endif

inline float sqrf(float x) { return x * x; }

#ifndef Cube
template <typename T>
inline T Cube(T val) { return val * val * val; }
#endif

#endif // VECTOR_SIMPLEMATH_H
