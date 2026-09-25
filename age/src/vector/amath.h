////////////////////////////////////////
// amath.h
//
// Umbrella math header: pulls in the vector/matrix types and the scalar
// helpers (constants, Clamp/Lerp/Wrap/Min/Max/Abs).
////////////////////////////////////////

#ifndef VECTOR_AMATH_H
#define VECTOR_AMATH_H

#include <math.h>

#include "vector/vector3.h"
#include "vector/Matrix34.h"
#include "vector/matrix44.h"

////////////////////////////////////////////////////////////////////////////
// Constants

#define PI		3.14159265358979323846f
#define HALF_PI	(PI*0.5f)
#define TWO_PI	(PI*2.0f)
#define DtoR	(PI/180.0f)
#define RtoD	(180.0f/PI)
#define PH_DEG2RAD(deg) ((deg) * DtoR)
#define PH_RAD2DEG(rad) ((rad) * RtoD)
#define SQRT2DIV2 0.70710678118654752440f

////////////////////////////////////////////////////////////////////////////
// Scalar helpers (templated so they work for int/float alike)

// NOTE: Clamp/Min/Max live as macros in core/types.h (shared framework, owned by
// the parallel track) — don't redefine them here or the macro clobbers the
// template.  Abs/Lerp remain template helpers.
template<class T> inline T Abs(T a)					{return a<0?-a:a;}
// Lerp is (t, a, b) -- the AGE convention, matching the non-template float
// overload in core/types.h.  This used to be declared (a, b, t) here: for an
// all-float call the non-template won and the result was right, but any call
// with a non-float operand silently bound to this template and interpolated
// garbage.  The member Vector3::Lerp(a, b, t) is a different function.
template<class T> inline T Lerp(float t,T a,T b)		{return (T)(a+(b-a)*t);}
template<class T> inline bool SameSign(T a, T b)		{return (a >= 0 && b >= 0) || (a < 0 && b < 0);}

// Wrap v into [0,range), or the 3-arg form into [lo,hi).
inline float Wrap(float v,float range)
{
	if(range<=0.0f) return v;
	v=fmodf(v,range);
	if(v<0.0f) v+=range;
	return v;
}
inline float Wrap(float v,float lo,float hi)		{return lo+Wrap(v-lo,hi-lo);}
inline int Wrap(int v, int lo, int hi) {
	int range = hi - lo + 1;
	if (range <= 0) return lo;
	int res = (v - lo) % range;
	if (res < 0) res += range;
	return lo + res;
}

inline bool Approach(float &value, float goal, float rate, float dt) {
	if (value == goal) return true;
	float delta = goal - value;
	float maxChange = rate * dt;
	if (fabsf(delta) <= maxChange) {
		value = goal;
		return true;
	}
	if (delta > 0.0f) {
		value += maxChange;
	} else {
		value -= maxChange;
	}
	return false;
}

// Ease curves on [0,1]: SlowIn starts slow, SlowOut ends slow, SlowInOut is
// the smoothstep bell (used by mcprogress as a bell-curve weight).
inline float SlowIn(float t) {
	return t * t;
}
inline float SlowOut(float t) {
	return 1.0f - (1.0f - t) * (1.0f - t);
}
inline float SlowInOut(float t) {
	return t * t * (3.0f - 2.0f * t);
}

inline float BellInOut(float t) {
	if (t > 1.0f) {
		return sinf(t * (PI / 64.0f));
	}
	return t * t * (3.0f - 2.0f * t);
}

#endif // VECTOR_AMATH_H
