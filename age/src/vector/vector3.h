////////////////////////////////////////
// vector3.h
////////////////////////////////////////

#ifndef VECTOR_VECTOR3_H
#define VECTOR_VECTOR3_H

#include <math.h>
#include <stdio.h>

inline float invsqrtf(float val) { return 1.0f / sqrtf(val); }

#ifndef PI
#define PI 3.14159265358979323846f
#endif

inline void cos_and_sin(float &c, float &s, float theta) {
    c = cosf(theta);
    s = sinf(theta);
}

#include "vector/vector2.h"
#include "vector/simplemath.h"
class Matrix34;

////////////////////////////////////////////////////////////////////////////

class Vector3
{
public:
	Vector3()										{}
	Vector3(float X,float Y,float Z)				{x=X;y=Y;z=Z;}
	Vector3(class datResource &rsc)					{}

	//// Access ////
	void Set(float X,float Y,float Z)				{x=X;y=Y;z=Z;}
	void Set(float s)								{x=y=z=s;}
	void Set(const Vector3 &v)						{x=v.x;y=v.y;z=v.z;}
	void Zero()										{x=y=z=0.0f;}
	// Ground-plane set from a Vector2 (x -> x, y -> z), height zeroed.
	void SetFlat(const Vector2 &v)				{x=v.x;y=0.0f;z=v.y;}
	void SetFlat(const Vector3 &v)				{x=v.x;y=0.0f;z=v.z;}
	void SetFlat(float _x, float _z)			{x=_x;y=0.0f;z=_z;}
	void GetVector2XZ(Vector2 &out) const		{out.x=x;out.y=z;}
	float &operator[](int i)						{return (&x)[i];}
	float operator[](int i) const					{return (&x)[i];}

	//// Operators ////
	Vector3 operator-() const						{return Vector3(-x,-y,-z);}
	Vector3 operator+(const Vector3 &v) const		{return Vector3(x+v.x,y+v.y,z+v.z);}
	Vector3 operator-(const Vector3 &v) const		{return Vector3(x-v.x,y-v.y,z-v.z);}
	Vector3 operator*(float s) const				{return Vector3(x*s,y*s,z*s);}
	Vector3 operator*(const Vector3 &v) const		{return Vector3(x*v.x,y*v.y,z*v.z);}
	Vector3 operator/(float s) const				{float r=1.0f/s; return Vector3(x*r,y*r,z*r);}
	void operator+=(const Vector3 &v)				{x+=v.x;y+=v.y;z+=v.z;}
	void operator-=(const Vector3 &v)				{x-=v.x;y-=v.y;z-=v.z;}
	void operator*=(float s)						{x*=s;y*=s;z*=s;}
	void operator*=(const Vector3 &v)				{x*=v.x;y*=v.y;z*=v.z;}
	void operator/=(float s)						{float r=1.0f/s;x*=r;y*=r;z*=r;}
	bool operator==(const Vector3 &v) const			{return x==v.x&&y==v.y&&z==v.z;}
	bool operator!=(const Vector3 &v) const			{return !(*this==v);}

	//// Operations (in-place, the original engine convention) ////
	void Add(const Vector3 &v)						{x+=v.x;y+=v.y;z+=v.z;}
	void Add(const Vector3 &a,const Vector3 &b)		{x=a.x+b.x;y=a.y+b.y;z=a.z+b.z;}
	void Add(float X, float Y, float Z)				{x+=X;y+=Y;z+=Z;}
	void Add(float s)								{x+=s;y+=s;z+=s;}
	void Sub(const Vector3 &v)						{x-=v.x;y-=v.y;z-=v.z;}
	void Sub(const Vector3 &a,const Vector3 &b)		{x=a.x-b.x;y=a.y-b.y;z=a.z-b.z;}
	void Subtract(const Vector3 &v)					{x-=v.x;y-=v.y;z-=v.z;}
	void Subtract(const Vector3 &a,const Vector3 &b)	{x=a.x-b.x;y=a.y-b.y;z=a.z-b.z;}
	void Scale(float s)								{x*=s;y*=s;z*=s;}
	void Scale(const Vector3 &v,float s)			{x=v.x*s;y=v.y*s;z=v.z*s;}
	void SetScaled(const Vector3 &v,float s)		{x=v.x*s;y=v.y*s;z=v.z*s;}
	void Multiply(const Vector3 &v)					{x*=v.x;y*=v.y;z*=v.z;}
	void Multiply(float s)							{x*=s;y*=s;z*=s;}
	void Negate()									{x=-x;y=-y;z=-z;}
	void Negate(const Vector3 &v)					{x=-v.x;y=-v.y;z=-v.z;}
	void Abs()										{if(x<0.0f)x=-x;if(y<0.0f)y=-y;if(z<0.0f)z=-z;}
	void Average(const Vector3 &v)					{ x = (x + v.x)*0.5f; y = (y + v.y)*0.5f; z = (z + v.z)*0.5f; }
	void Average(const Vector3 &v1, const Vector3 &v2) { x = (v1.x + v2.x)*0.5f; y = (v1.y + v2.y)*0.5f; z = (v1.z + v2.z)*0.5f; }
	void Average(const Vector2 &v1, const Vector2 &v2) { x = (v1.x + v2.x)*0.5f; y = 0.0f; z = (v1.y + v2.y)*0.5f; }
	void Average(const Vector2 &v)					{ x = (x + v.x)*0.5f; z = (z + v.y)*0.5f; }
	void AddScaled(const Vector3 &v, float s)		{x+=v.x*s;y+=v.y*s;z+=v.z*s;}
	void AddScaled(const Vector3 &v)                {x+=v.x; y+=v.y; z+=v.z;}
	void AddScaled(const Vector3 &v1, const Vector3 &v2, float s) {
		x = v1.x + v2.x * s;
		y = v1.y + v2.y * s;
		z = v1.z + v2.z * s;
	}
	void InvScale(float s) {
		float r = 1.0f / s;
		x *= r; y *= r; z *= r;
	}
	void InvScale(const Vector3 &v, float s) {
		float r = 1.0f / s;
		x = v.x * r; y = v.y * r; z = v.z * r;
	}

	float Dot(const Vector3 &v) const				{return x*v.x+y*v.y+z*v.z;}
	float Dot3(const Vector3 &v) const				{return Dot(v);}

	void Cross(const Vector3 &v)					{Cross(*this,v);}
	void Cross(const Vector3 &a,const Vector3 &b)
	{
		float cx=a.y*b.z-a.z*b.y;
		float cy=a.z*b.x-a.x*b.z;
		float cz=a.x*b.y-a.y*b.x;
		x=cx;y=cy;z=cz;
	}

	float MagSq() const								{return x*x+y*y+z*z;}
	void Sin() { x = sinf(x); y = sinf(y); z = sinf(z); }   // component-wise
	void Cos() { x = cosf(x); y = cosf(y); z = cosf(z); }
	float Mag() const								{return sqrtf(MagSq());}
	float Mag2() const								{return MagSq();}
	float FlatMag() const                           {return sqrtf(x*x+z*z);}
	float FlatMag2() const                          {return x*x+z*z;}

	bool IsZero() const { return x == 0.0f && y == 0.0f && z == 0.0f; }
	bool IsNonZero() const { return x != 0.0f || y != 0.0f || z != 0.0f; }

	void Min(const Vector3 &v) {
		if (v.x < x) x = v.x;
		if (v.y < y) y = v.y;
		if (v.z < z) z = v.z;
	}
	void Max(const Vector3 &v) {
		if (v.x > x) x = v.x;
		if (v.y > y) y = v.y;
		if (v.z > z) z = v.z;
	}

	void Clamp(float minVal, float maxVal) {
		if (x < minVal) x = minVal; else if (x > maxVal) x = maxVal;
		if (y < minVal) y = minVal; else if (y > maxVal) y = maxVal;
		if (z < minVal) z = minVal; else if (z > maxVal) z = maxVal;
	}

	float GetX() const { return x; }
	float GetY() const { return y; }
	float GetZ() const { return z; }

	float Normalize()
	{
		float m=Mag();
		if(m>0.0f){float r=1.0f/m;x*=r;y*=r;z*=r;}
		return m;
	}
	float Normalize(const Vector3 &v)
	{
		*this = v;
		return Normalize();
	}

	void Invert()
	{
		x=1.0f/x; y=1.0f/y; z=1.0f/z;
	}
	void Invert(const Vector3 &v)
	{
		x=1.0f/v.x; y=1.0f/v.y; z=1.0f/v.z;
	}
	void SubtractScaled(const Vector3 &v1, const Vector3 &v2, float scale)
	{
		x=v1.x-v2.x*scale; y=v1.y-v2.y*scale; z=v1.z-v2.z*scale;
	}
	void SubtractScaled(const Vector3 &v, float scale)
	{
		x -= v.x * scale; y -= v.y * scale; z -= v.z * scale;
	}

	float DistSq(const Vector3 &v) const			{return Vector3(x-v.x,y-v.y,z-v.z).MagSq();}
	float Dist2(const Vector3 &v) const				{return DistSq(v);}
	float Dist(const Vector3 &v) const				{return sqrtf(DistSq(v));}
	bool IsEqual(const Vector3 &v, float tolerance = 1e-4f) const {
		return fabsf(x - v.x) < tolerance && fabsf(y - v.y) < tolerance && fabsf(z - v.z) < tolerance;
	}
	Vector3 operator%(const Vector3 &v) const {
		Vector3 r;
		r.Cross(*this, v);
		return r;
	}
	float operator^(const Vector3 &v) const {
		return Dot(v);
	}
	float Angle(const Vector3 &v) const {
		float m1 = Mag();
		float m2 = v.Mag();
		if (m1 > 0.0f && m2 > 0.0f) {
			float val = Dot(v) / (m1 * m2);
			if (val < -1.0f) val = -1.0f;
			if (val > 1.0f) val = 1.0f;
			return acosf(val);
		}
		return 0.0f;
	}
	bool IsClose(const Vector3 &v, float tolerance) const {
		return IsEqual(v, tolerance);
	}
	float FlatDist2(const Vector3 &v) const         {return (x-v.x)*(x-v.x) + (z-v.z)*(z-v.z);}
	float FlatDist(const Vector3 &v) const          {return sqrtf(FlatDist2(v));}
	float FlatDot(const Vector3 &v) const           {return x*v.x + z*v.z;}
	float CrossY(const Vector3 &v) const            {return z*v.x - x*v.z;}
	float InvMag() const                            {float m = Mag2(); return m > 0.0f ? 1.0f / sqrtf(m) : 0.0f;}

	void Lerp(const Vector3 &a,const Vector3 &b,float t)
	{
		x=a.x+(b.x-a.x)*t;
		y=a.y+(b.y-a.y)*t;
		z=a.z+(b.z-a.z)*t;
	}

	void Lerp(float t,const Vector3 &a,const Vector3 &b)
	{
		x=a.x+(b.x-a.x)*t;
		y=a.y+(b.y-a.y)*t;
		z=a.z+(b.z-a.z)*t;
	}

	//// Rotate this vector about a world axis (in-place) ////
	void RotateX(float ang)							{float s=sinf(ang),c=cosf(ang),ny=c*y-s*z,nz=s*y+c*z;y=ny;z=nz;}
	void RotateY(float ang)							{float s=sinf(ang),c=cosf(ang),nx=c*x+s*z,nz=-s*x+c*z;x=nx;z=nz;}
	void RotateZ(float ang)							{float s=sinf(ang),c=cosf(ang),nx=c*x-s*y,ny=s*x+c*y;x=nx;y=ny;}

	void RotateAboutAxis(const Vector3 &axis, float angle) {
		float sinAng = sinf(angle), cosAng = cosf(angle);
		Vector3 u = axis;
		u.Normalize();
		float dot = Dot(u);
		Vector3 cross;
		cross.Cross(u, *this);
		x = x * cosAng + cross.x * sinAng + u.x * dot * (1.0f - cosAng);
		y = y * cosAng + cross.y * sinAng + u.y * dot * (1.0f - cosAng);
		z = z * cosAng + cross.z * sinAng + u.z * dot * (1.0f - cosAng);
	}

	void RotateAboutAxis(float angle, char axis) {
		if (axis == 'x' || axis == 'X') {
			RotateX(angle);
		} else if (axis == 'y' || axis == 'Y') {
			RotateY(angle);
		} else if (axis == 'z' || axis == 'Z') {
			RotateZ(angle);
		}
	}

	//// Transform this vector by a matrix's 3x3 part (in-place) ////
	void Dot3x3(const Matrix34 &m);
	void Dot3x3Transpose(const Matrix34 &m);
	void Dot(const Matrix34 &m);
	void Dot3x3(const Vector3 &in, const Matrix34 &m);
	void Dot(const Vector3 &in, const Matrix34 &m);

	bool Approach(const Vector3 &goal, float rate, float dt) {
		Vector3 delta;
		delta.Subtract(goal, *this);
		float dist = delta.Mag();
		float maxChange = rate * dt;
		if (dist <= maxChange) {
			*this = goal;
			return true;
		}
		AddScaled(delta, maxChange / dist);
		return false;
	}

	void Print(const char *name, bool flag = true) const {
		if (name) {
			printf("%s: (%f, %f, %f)\n", name, x, y, z);
		} else {
			printf("(%f, %f, %f)\n", x, y, z);
		}
	}
	float x;	// +00
	float y;	// +04
	float z;	// +08
};

extern const Vector3 XAXIS;
extern const Vector3 YAXIS;
extern const Vector3 ZAXIS;
extern const Vector3 ORIGIN;

//// Free operators ////
inline Vector3 operator*(float s, const Vector3 &v)	{return Vector3(v.x*s,v.y*s,v.z*s);}

#ifndef DtoR
#define DtoR 0.017453292519943295f
#endif

#endif // VECTOR_VECTOR3_H
