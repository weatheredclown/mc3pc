////////////////////////////////////////
// Matrix34.h
////////////////////////////////////////

#ifndef VECTOR_MATRIX34_H
#define VECTOR_MATRIX34_H

#include "core/output.h"
#include "core/types.h"
#include "vector/vector3.h"
#include "vector/quaternion.h"
#include "vector/matrix44.h"

////////////////////////////////////////////////////////////////////////////
//
// 3x4 affine transform: rows a,b,c are the 3x3 rotation/scale basis, d is the
// translation.  Row-major, the original engine convention (point' = p.x*a + p.y*b + p.z*c + d).
//
////////////////////////////////////////////////////////////////////////////

class Matrix34
{
public:
	Matrix34()										{}
	Matrix34(class datResource &rsc)				{}

	static const Matrix34 I;						// identity

	//// Identity ////
	void Identity()									{Identity3x3();d.Zero();}
	void Identity3x3()								{a.Set(1,0,0);b.Set(0,1,0);c.Set(0,0,1);}
	void Zero3x3()                                  {a.Zero(); b.Zero(); c.Zero();}
	float Determinant3x3() const {
		return a.x*(b.y*c.z - b.z*c.y) - a.y*(b.x*c.z - b.z*c.x) + a.z*(b.x*c.y - b.y*c.x);
	}

    void UnTransform(const class Vector3 &in, class Vector3 &out) const
    {
        Vector3 temp = in;
        temp.Subtract(d);
        out.x = temp.x * a.x + temp.y * a.y + temp.z * a.z;
        out.y = temp.x * b.x + temp.y * b.y + temp.z * b.z;
        out.z = temp.x * c.x + temp.y * c.y + temp.z * c.z;
    }
    void UnTransform(Vector3 &v) const
    {
        Vector3 temp = v;
        UnTransform(temp, v);
    }

	//// Access ////
	void Print(const char *name = 0) const { Quitf("Matrix34::Print - not implemented"); }
	void Set3x3(const Matrix34 &m)					{a=m.a;b=m.b;c=m.c;}
	void Set(const Matrix34 &m)						{a=m.a;b=m.b;c=m.c;d=m.d;}
	void ToMatrix34(Matrix34 &out) const            {out=*this;}

	// Rotate*: rotate this frame about the WORLD axis (3x3 only, d untouched):
	// this = this * R(angle), i.e. each basis row rotates like a Vector3 would.
	// (RotateFull* is the same composition including d; RotateLocal* rotates
	// about the matrix's own axis, this = R * this.)  rb camera code composes
	// pitch-then-yaw with these and needs the world-axis order.
	void RotateX(float angle)
	{
		a.RotateX(angle); b.RotateX(angle); c.RotateX(angle);
	}

	// Make* rotations write the 3x3 ONLY and preserve d, matching MakeRotate()
	// and the original AGE semantics.  rb code depends on this: bone LocalMtx.d
	// holds the skeleton offset (elbowcraneik.cpp), and several callers set d
	// before the Make call (mountedweapon.cpp, scroni XCommand.cpp, editgraph).
	void MakeRotateX(float angle)
	{
		float sinAng=sinf(angle),cosAng=cosf(angle);
		a.Set(1.0f, 0.0f, 0.0f);
		b.Set(0.0f, cosAng, sinAng);
		c.Set(0.0f, -sinAng, cosAng);
	}

	void RotateY(float angle)
	{
		a.RotateY(angle); b.RotateY(angle); c.RotateY(angle);
	}

	void MakeRotateY(float angle)
	{
		float sinAng=sinf(angle),cosAng=cosf(angle);
		a.Set(cosAng, 0.0f, -sinAng);
		b.Set(0.0f, 1.0f, 0.0f);
		c.Set(sinAng, 0.0f, cosAng);
	}

	void UnTransform3x3(const Vector3 &in, Vector3 &out) const
	{
		float ix=in.x,iy=in.y,iz=in.z;
		out.x = ix * a.x + iy * a.y + iz * a.z;
		out.y = ix * b.x + iy * b.y + iz * b.z;
		out.z = ix * c.x + iy * c.y + iz * c.z;
	}

	void UnTransform3x3(Vector3 &v) const
	{
		Vector3 temp = v;
		UnTransform3x3(temp, v);
	}

	//// Transform a point by the full 3x4 (out = in*3x3 + d) ////
	void Transform(const Vector3 &in,Vector3 &out) const
	{
		float ix=in.x,iy=in.y,iz=in.z;
		out.x=ix*a.x+iy*b.x+iz*c.x+d.x;
		out.y=ix*a.y+iy*b.y+iz*c.y+d.y;
		out.z=ix*a.z+iy*b.z+iz*c.z+d.z;
	}
	void Transform(Vector3 &v) const
	{
		Transform(v, v);
	}

	//// Rotate a point by the 3x3 only (no translation) ////
	void Transform3x3(const Vector3 &in,Vector3 &out) const
	{
		float ix=in.x,iy=in.y,iz=in.z;
		out.x=ix*a.x+iy*b.x+iz*c.x;
		out.y=ix*a.y+iy*b.y+iz*c.y;
		out.z=ix*a.z+iy*b.z+iz*c.z;
	}
	void Transform3x3(Vector3 &v) const
	{
		Transform3x3(v, v);
	}

	void RotateZ(float angle)
	{
		a.RotateZ(angle); b.RotateZ(angle); c.RotateZ(angle);
	}

	void MakeTranslate(const Vector3 &t)
	{
		a.Set(1.0f, 0.0f, 0.0f);
		b.Set(0.0f, 1.0f, 0.0f);
		c.Set(0.0f, 0.0f, 1.0f);
		d = t;
	}

	// Rows a/b/c are the rotated basis vectors (Transform3x3 = in.x*a + in.y*b
	// + in.z*c), so a positive angle turns y toward z about +x exactly like
	// MakeRotateX.  PC port: this used to build the transpose (a rotation by
	// -angle), which made every Rotate(axis, ...) caller - the vehicle collider's
	// angular integration above all - spin the wrong way: a nose-down pitch fed
	// the suspension a torque that pitched the car further, and the car flipped
	// off the grid within a tenth of a second.
	void MakeRotate(const Vector3 &axis, float angle)
	{
		float cosAng = cosf(angle);
		float sinAng = sinf(angle);
		float t = 1.0f - cosAng;
		Vector3 u = axis;
		u.Normalize();

		a.x = t * u.x * u.x + cosAng;
		a.y = t * u.x * u.y + sinAng * u.z;
		a.z = t * u.x * u.z - sinAng * u.y;

		b.x = t * u.x * u.y - sinAng * u.z;
		b.y = t * u.y * u.y + cosAng;
		b.z = t * u.y * u.z + sinAng * u.x;

		c.x = t * u.x * u.z + sinAng * u.y;
		c.y = t * u.y * u.z - sinAng * u.x;
		c.z = t * u.z * u.z + cosAng;
	}

	void MakeRotateUnitAxis(const Vector3 &axis, float angle)
	{
		MakeRotate(axis, angle);
	}

	// MakeRotateX/MakeRotateY are defined above (3x3-only, preserve d).
	void MakeRotateZ(float angle) { MakeRotate(ZAXIS, angle); }

	void FromEulersXYZ(const Vector3 &e)
	{
		float cx = cosf(e.x), sx = sinf(e.x);
		float cy = cosf(e.y), sy = sinf(e.y);
		float cz = cosf(e.z), sz = sinf(e.z);

		a.Set(cy*cz, cy*sz, -sy);
		b.Set(sx*sy*cz - cx*sz, sx*sy*sz + cx*cz, sx*cy);
		c.Set(cx*sy*cz + sx*sz, cx*sy*sz - sx*cz, cx*cy);
	}

	// Apply Z, then X, then Y (Y last = world yaw).  Generic builder in
	// Matrix34.cpp; the old inline body was Ry*Rx*Rz (reverse-order naming).
	void FromEulersZXY(const Vector3 &e) { FromEulers(e, "zxy"); }

	void MakeScale(const Vector3 &s)
	{
		a.Set(s.x, 0, 0);
		b.Set(0, s.y, 0);
		c.Set(0, 0, s.z);
		d.Zero();
	}

	void MakeScale(float s)
	{
		a.Set(s, 0, 0);
		b.Set(0, s, 0);
		c.Set(0, 0, s);
		d.Zero();
	}

	void MakeScaleFull(float s) { MakeScale(s); }
	void MakeScaleFull(float sx, float sy, float sz) { MakeScale(Vector3(sx, sy, sz)); }
	void MakeScaleFull(const Vector3 &s) { MakeScale(s); }

	void MakeRotX(float angle) { MakeRotateX(angle); }
	void MakeRotY(float angle) { MakeRotateY(angle); }
	void MakeRotZ(float angle) { MakeRotateZ(angle); }

	void Translate(const Vector3 &v) { d += v; }
	void Translate(float x, float y, float z) { d.x += x; d.y += y; d.z += z; }

	void Scale(float sx, float sy, float sz)
	{
		a.x *= sx; a.y *= sx; a.z *= sx;
		b.x *= sy; b.y *= sy; b.z *= sy;
		c.x *= sz; c.y *= sz; c.z *= sz;
	}

	void Scale(const Vector3 &s)
	{
		Matrix34 r;
		r.MakeScale(s);
		Dot3x3(*this, r);
	}

	void Scale(float s)
	{
		a.Scale(s);b.Scale(s);c.Scale(s);
	}

	// RotateLocal*: rotate about the matrix's OWN axis, i.e. this = R(angle) * this
	// (pre-multiply, rows mix), with the same handedness as MakeRotate*.  The
	// previous bodies used the opposite sign (R(-angle)), which put the crane
	// claw's ClampOffset behind the forearm instead of in front of it, so the
	// IK could never reach its target (elbowcraneik.cpp: RotateLocalX(PI/2)).
	void RotateLocalZ(float angle)
	{
		float sinAng = sinf(angle), cosAng = cosf(angle);
		Vector3 ra = a, rb = b;
		a.x = ra.x * cosAng + rb.x * sinAng; a.y = ra.y * cosAng + rb.y * sinAng; a.z = ra.z * cosAng + rb.z * sinAng;
		b.x = -ra.x * sinAng + rb.x * cosAng; b.y = -ra.y * sinAng + rb.y * cosAng; b.z = -ra.z * sinAng + rb.z * cosAng;
	}

	void RotateLocalX(float angle)
	{
		float sinAng = sinf(angle), cosAng = cosf(angle);
		Vector3 rb = b, rc = c;
		b.x = rb.x * cosAng + rc.x * sinAng; b.y = rb.y * cosAng + rc.y * sinAng; b.z = rb.z * cosAng + rc.z * sinAng;
		c.x = -rb.x * sinAng + rc.x * cosAng; c.y = -rb.y * sinAng + rc.y * cosAng; c.z = -rb.z * sinAng + rc.z * cosAng;
	}

	void RotateLocalY(float angle)
	{
		float sinAng = sinf(angle), cosAng = cosf(angle);
		Vector3 ra = a, rc = c;
		a.x = ra.x * cosAng - rc.x * sinAng; a.y = ra.y * cosAng - rc.y * sinAng; a.z = ra.z * cosAng - rc.z * sinAng;
		c.x = ra.x * sinAng + rc.x * cosAng; c.y = ra.y * sinAng + rc.y * cosAng; c.z = ra.z * sinAng + rc.z * cosAng;
	}

	void RotateFullX(float angle)
	{
		Matrix34 r;
		r.MakeRotate(XAXIS, angle);
		Dot(*this, r);
	}
	void RotateFullY(float angle)
	{
		Matrix34 r;
		r.MakeRotate(YAXIS, angle);
		Dot(*this, r);
	}
	void RotateFullZ(float angle)
	{
		Matrix34 r;
		r.MakeRotate(ZAXIS, angle);
		Dot(*this, r);
	}

	// Euler angles for an arbitrary axis order.  `order` is a 3-letter string
	// naming the axes in APPLICATION order ("xyz" = rotate about X, then Y,
	// then Z, each about the parent/world axis), i.e. this = Rx * Ry * Rz --
	// the same convention FromEulersXYZ / FromEulersXZY are built with, so
	// FromEulers(GetEulers(o), o) round-trips.  mc3 reads the car heading with
	// GetEulers("zxy").y (Y applied last = world yaw).  Bodies in Matrix34.cpp.
	Vector3 GetEulers(const char *order) const;
	Vector3 GetEulers() const { return GetEulers("xyz"); }
	Vector3 GetEulersFast() const { return GetEulers(); }
	void GetEulers(Vector3 &e, const char *order) const { e = GetEulers(order); }

	void MakeUpright()
	{
		b.Set(0.0f, 1.0f, 0.0f);
		c.y = 0.0f;
		c.Normalize();
		a.Cross(b, c);
		a.Normalize();
	}

	void Inverse() { FastInverse(); }
	void Normalize() { a.Normalize(); b.Normalize(); c.Normalize(); }

	void ScaleFull(float scale)
	{
		a.Scale(scale);
		b.Scale(scale);
		c.Scale(scale);
	}

	void Interpolate(const Matrix34 &m1, const Matrix34 &m2, float t)
	{
		a.Lerp(m1.a, m2.a, t);
		b.Lerp(m1.b, m2.b, t);
		c.Lerp(m1.c, m2.c, t);
		d.Lerp(m1.d, m2.d, t);
		Normalize();
	}

	void Zero()
	{
		a.Zero();
		b.Zero();
		c.Zero();
		d.Zero();
	}

	//// Extract XYZ euler angles (inverse of FromEulersXYZ). ////
	void ToEulersXYZ(Vector3 &eulers) const { eulers = GetEulers("xyz"); }

	void Rotate(const Vector3 &axis, float angle)
	{
		Matrix34 r;
		r.MakeRotate(axis, angle);
		Dot3x3(*this, r);
	}

	void RotateUnitAxis(const Vector3 &axis, float angle)
	{
		Matrix34 r;
		r.MakeRotateUnitAxis(axis, angle);
		Dot(*this, r);
	}

	void RotateTo(const Vector3 &from, const Vector3 &to, float factor = 1.0f)
	{
		Vector3 axis;
		axis.Cross(from, to);
		float mag = axis.Mag();
		if (mag > 0.0001f)
		{
			axis.Scale(1.0f / mag);
			float angle = from.Angle(to) * factor;
			Rotate(axis, angle);
		}
	}

	void FastInverse(const Matrix34 &mat)
	{
		a.x = mat.a.x; a.y = mat.b.x; a.z = mat.c.x;
		b.x = mat.a.y; b.y = mat.b.y; b.z = mat.c.y;
		c.x = mat.a.z; c.y = mat.b.z; c.z = mat.c.z;
		float tx = mat.d.x, ty = mat.d.y, tz = mat.d.z;
		d.x = -(tx * a.x + ty * b.x + tz * c.x);
		d.y = -(tx * a.y + ty * b.y + tz * c.y);
		d.z = -(tx * a.z + ty * b.z + tz * c.z);
	}

	void FastInverse()
	{
		Matrix34 temp = *this;
		FastInverse(temp);
	}

	void Dot3x3(const Matrix34 &y)
	{
		Matrix34 temp = *this;
		Dot3x3(temp, y);
	}

	void Dot(const Matrix34 &y)
	{
		Matrix34 temp = *this;
		Dot(temp, y);
	}

	//// Build the 3x3 rotation from XZY euler angles (rotate order X, then Z, then
	//// Y).
	void FromEulersZYX(const Vector3 &e);
	void FromEulers(const Vector3 &e) { FromEulersXZY(e); }   // engine default order
	void FromEulers(const Vector3 &e, const char *order);      // any xyz permutation, application order
	void FromEulersXZY(const Vector3 &e)
	{
		// Transcribed verbatim from the original engine (rb/cranimation/frame.cpp
		// FromEulers) so the matrix is bit-for-bit the convention the .anim data
		// was authored against.  Same a/b/c row layout as rb's Matrix34.
		float cx = cosf(e.x), sx = sinf(e.x);
		float cy = cosf(e.y), sy = sinf(e.y);
		float cz = cosf(e.z), sz = sinf(e.z);
		a.Set(  cz*cy,             sz,    -cz*sy);
		b.Set( -cx*sz*cy + sx*sy,  cx*cz,  cx*sz*sy + sx*cy);
		c.Set(  sx*sz*cy + cx*sy, -sx*cz, -sx*sz*sy + cx*cy);
	}

	//// Extract XZY euler angles from the 3x3 (inverse of FromEulersXZY). ////
	void ToEulersXZY(Vector3 &e) const { e = GetEulers("xzy"); }

	// Polar camera support (editor orbit camera).  Azimuth about +Y, incline
	// measured from +Y (1.57 = horizon).  PolarView builds an orientation whose
	// boom row (c) points from the focus to the eye, with d = the eye offset
	// from the focus at `dist`; the caller adds the focal point afterwards.
	void PolarView(float dist, float azimuth, float incline)
	{
		float si = sinf(incline), ci = cosf(incline);
		float sa = sinf(azimuth), ca = cosf(azimuth);
		Vector3 boom(si * sa, ci, si * ca);            // unit, focus -> eye
		c.Set(boom);
		Vector3 up(0.0f, 1.0f, 0.0f);
		if (ci > 0.999f || ci < -0.999f)
			up.Set(0.0f, 0.0f, ci > 0.0f ? 1.0f : -1.0f);
		a.Cross(up, c); a.Normalize();
		b.Cross(c, a);
		d.SetScaled(boom, dist);
	}

	// Inverse of PolarView: polarCoords = (0, azimuth, incline); polarOffset =
	// the focal point implied by d and the boom at `dist`.
	void GetPolar(Vector3 *polarCoords, Vector3 *polarOffset, float dist) const
	{
		float cy = c.y < -1.0f ? -1.0f : (c.y > 1.0f ? 1.0f : c.y);
		float incline = acosf(cy);
		float azimuth = atan2f(c.x, c.z);
		if (polarCoords)
			polarCoords->Set(0.0f, azimuth, incline);
		if (polarOffset)
		{
			polarOffset->SetScaled(c, -dist);
			polarOffset->Add(d);
		}
	}

	void LookAt(const Vector3 &from, const Vector3 &to)
	{
		// AGE is natively right-handed (OpenGL): a camera looks down -Z, so the
		// forward/z basis column (c) is the BOOM -- the direction from the target
		// back to the eye (from - to), not the view direction. rb camera code
		// relies on this: it reads the camera azimuth straight off c via
		// atan2(c.x, c.z), expecting c to be the eye-offset direction. Building c
		// as the view direction (to - from) instead leaves that read-back 180 deg
		// off, which makes the third-person camera strobe to the opposite side
		// every frame. gfxRenderState::SetCamera() looks down -c to match.
		d = from;
		Vector3 boom = from - to;
		boom.Normalize();
		Vector3 up(0.0f, 1.0f, 0.0f);
		if (fabsf(boom.y) > 0.999f) {
			up.Set(0.0f, 0.0f, boom.y > 0.0f ? 1.0f : -1.0f);
		}
		Vector3 right;
		right.Cross(up, boom);
		right.Normalize();
		Vector3 actualUp;
		actualUp.Cross(boom, right);
		actualUp.Normalize();

		a = right;
		b = actualUp;
		c = boom;
	}

	void LookAt(const Vector3 &to)
	{
		LookAt(d, to);
	}

	// Orient the rotation so the camera looks ALONG `dir`, leaving d alone.
	// AGE cameras look down -c, so c is the boom: the reverse of the view
	// direction (see LookAt above).  camAppCS::UpdateApproach calls this on a
	// rotation-only scratch matrix to build the goal orientation implied by
	// the interest point (alpha Matrix34::LookDown, 0x2376a0).
	void LookDown(const Vector3 &dir)
	{
		Vector3 boom(-dir.x, -dir.y, -dir.z);
		if (boom.Mag2() < 1e-12f) {
			a = XAXIS; b = YAXIS; c = ZAXIS;
			return;
		}
		boom.Normalize();
		Vector3 up(0.0f, 1.0f, 0.0f);
		if (fabsf(boom.y) > 0.999f)
			up.Set(0.0f, 0.0f, boom.y > 0.0f ? 1.0f : -1.0f);
		a.Cross(up, boom); a.Normalize();
		b.Cross(boom, a);  b.Normalize();
		c = boom;
	}

	//// Full 3x4 compose: this = x * y (rotation and translation). ////
	void Dot(const Matrix34 &x,const Matrix34 &y)
	{
		Matrix34 r; r.Dot3x3(x,y);
		Vector3 td=x.d; td.Dot3x3(y); td.Add(y.d);
		a=r.a; b=r.b; c=r.c; d=td;
	}
	void DotTranspose(const Matrix34 &x, const Matrix34 &y)
	{
		Matrix34 invY;
		invY.FastInverse(y);
		Dot(x, invY);
	}

	//// 3x3 concatenation: this = x * y ////
	void Dot3x3(const Matrix34 &x,const Matrix34 &y)
	{
		Vector3 ra=x.a,rb=x.b,rc=x.c;
		ra.Dot3x3(y);
		rb.Dot3x3(y);
		rc.Dot3x3(y);
		a=ra;b=rb;c=rc;
	}

	bool IsEqual(const Matrix34 &m, float tolerance = 1e-4f) const {
		return a.IsEqual(m.a, tolerance) && b.IsEqual(m.b, tolerance) && c.IsEqual(m.c, tolerance) && d.IsEqual(m.d, tolerance);
	}

	void FromQuaternion(const Quaternion &q)
	{
		float xx = q.x * q.x;
		float xy = q.x * q.y;
		float xz = q.x * q.z;
		float xw = q.x * q.w;
		float yy = q.y * q.y;
		float yz = q.y * q.z;
		float yw = q.y * q.w;
		float zz = q.z * q.z;
		float zw = q.z * q.w;

		a.x = 1.0f - 2.0f * (yy + zz);
		a.y = 2.0f * (xy + zw);
		a.z = 2.0f * (xz - yw);

		b.x = 2.0f * (xy - zw);
		b.y = 1.0f - 2.0f * (xx + zz);
		b.z = 2.0f * (yz + xw);

		c.x = 2.0f * (xz + yw);
		c.y = 2.0f * (yz - xw);
		c.z = 1.0f - 2.0f * (xx + yy);
	}

	void Inverse(const Matrix34 &m)
	{
		FastInverse(m);
	}
	void Invert(const Matrix34 &m) { Inverse(m); }
	void Invert() { Inverse(); }

	Vector3 a;	// +00  x-axis
	Vector3 b;	// +12  y-axis
	Vector3 c;	// +24  z-axis
	Vector3 d;	// +36  translation
};

inline void Matrix44::ToMatrix34(Matrix34 &out) const
{
	out.a.Set(a.x, a.y, a.z);
	out.b.Set(b.x, b.y, b.z);
	out.c.Set(c.x, c.y, c.z);
	out.d.Set(d.x, d.y, d.z);
}

////////////////////////////////////////////////////////////////////////////
// Vector3::Dot3x3 — defined here now that Matrix34 is complete.
// Transforms this vector by m's 3x3 (in-place).
inline void Vector3::Dot3x3(const Matrix34 &m)
{
	float ix=x,iy=y,iz=z;
	x=ix*m.a.x+iy*m.b.x+iz*m.c.x;
	y=ix*m.a.y+iy*m.b.y+iz*m.c.y;
	z=ix*m.a.z+iy*m.b.z+iz*m.c.z;
}

inline void Vector3::Dot3x3Transpose(const Matrix34 &m)
{
	float ix=x,iy=y,iz=z;
	x=ix*m.a.x+iy*m.a.y+iz*m.a.z;
	y=ix*m.b.x+iy*m.b.y+iz*m.b.z;
	z=ix*m.c.x+iy*m.c.y+iz*m.c.z;
}

inline void Vector3::Dot(const Matrix34 &m)
{
	float ix=x,iy=y,iz=z;
	x=ix*m.a.x+iy*m.b.x+iz*m.c.x+m.d.x;
	y=ix*m.a.y+iy*m.b.y+iz*m.c.y+m.d.y;
	z=ix*m.a.z+iy*m.b.z+iz*m.c.z+m.d.z;
}

inline void Vector3::Dot(const Vector3 &in, const Matrix34 &m)
{
	float ix=in.x,iy=in.y,iz=in.z;
	x=ix*m.a.x+iy*m.b.x+iz*m.c.x+m.d.x;
	y=ix*m.a.y+iy*m.b.y+iz*m.c.y+m.d.y;
	z=ix*m.a.z+iy*m.b.z+iz*m.c.z+m.d.z;
}

inline void Vector3::Dot3x3(const Vector3 &in, const Matrix34 &m)
{
	float ix=in.x,iy=in.y,iz=in.z;
	x=ix*m.a.x+iy*m.b.x+iz*m.c.x;
	y=ix*m.a.y+iy*m.b.y+iz*m.c.y;
	z=ix*m.a.z+iy*m.b.z+iz*m.c.z;
}

inline void Matrix44::FromMatrix34(const Matrix34 &mat)
{
	m[0] = mat.a.x; m[1] = mat.a.y; m[2] = mat.a.z; m[3] = 0.0f;
	m[4] = mat.b.x; m[5] = mat.b.y; m[6] = mat.b.z; m[7] = 0.0f;
	m[8] = mat.c.x; m[9] = mat.c.y; m[10] = mat.c.z; m[11] = 0.0f;
	m[12] = mat.d.x; m[13] = mat.d.y; m[14] = mat.d.z; m[15] = 1.0f;
}

inline void Matrix44::FastInverse(const Matrix34 &mat)
{
	Identity();
	m[0] = mat.a.x; m[1] = mat.b.x; m[2] = mat.c.x;
	m[4] = mat.a.y; m[5] = mat.b.y; m[6] = mat.c.y;
	m[8] = mat.a.z; m[9] = mat.b.z; m[10] = mat.c.z;
	float tx = mat.d.x, ty = mat.d.y, tz = mat.d.z;
	m[12] = -(tx * m[0] + ty * m[4] + tz * m[8]);
	m[13] = -(tx * m[1] + ty * m[5] + tz * m[9]);
	m[14] = -(tx * m[2] + ty * m[6] + tz * m[10]);
}

inline void Quaternion::FromMatrix34(const Matrix34 &m) {
	float trace = m.a.x + m.b.y + m.c.z;
	if (trace > 0.0f) {
		float s = 0.5f / sqrtf(trace + 1.0f);
		w = 0.25f / s;
		x = (m.b.z - m.c.y) * s;
		y = (m.c.x - m.a.z) * s;
		z = (m.a.y - m.b.x) * s;
	} else {
		if (m.a.x > m.b.y && m.a.x > m.c.z) {
			float s = 2.0f * sqrtf(1.0f + m.a.x - m.b.y - m.c.z);
			w = (m.b.z - m.c.y) / s;
			x = 0.25f * s;
			y = (m.a.y + m.b.x) / s;
			z = (m.a.z + m.c.x) / s;
		} else if (m.b.y > m.c.z) {
			float s = 2.0f * sqrtf(1.0f + m.b.y - m.a.x - m.c.z);
			w = (m.c.x - m.a.z) / s;
			x = (m.a.y + m.b.x) / s;
			y = 0.25f * s;
			z = (m.b.z + m.c.y) / s;
		} else {
			float s = 2.0f * sqrtf(1.0f + m.c.z - m.a.x - m.b.y);
			w = (m.a.y - m.b.x) / s;
			x = (m.a.z + m.c.x) / s;
			y = (m.b.z + m.c.y) / s;
			z = 0.25f * s;
		}
	}
}

inline void Quaternion::ToMatrix34(Matrix34 &m) const {
	float xx = x * x, yy = y * y, zz = z * z;
	float xy = x * y, xz = x * z, yz = y * z;
	float wx = w * x, wy = w * y, wz = w * z;

	m.a.x = 1.0f - 2.0f * (yy + zz);
	m.a.y = 2.0f * (xy + wz);
	m.a.z = 2.0f * (xz - wy);

	m.b.x = 2.0f * (xy - wz);
	m.b.y = 1.0f - 2.0f * (xx + zz);
	m.b.z = 2.0f * (yz + wx);

	m.c.x = 2.0f * (xz + wy);
	m.c.y = 2.0f * (yz - wx);
	m.c.z = 1.0f - 2.0f * (xx + yy);
}

#define M34_IDENTITY (Matrix34::I)

inline u32 PackEulersTo32(const Vector3 &e) {
    u32 x = (u32)(int)(e.x * (1024.0f / 3.14159265f)) & 0x7FF;
    u32 y = (u32)(int)(e.y * (1024.0f / 3.14159265f)) & 0x7FF;
    u32 z = (u32)(int)(e.z * (512.0f / 3.14159265f)) & 0x3FF;
    return (x << 21) | (y << 10) | z;
}

inline void UnpackEulersFrom32(Vector3 &out, u32 packed) {
    int x = (int)((packed >> 21) & 0x7FF);
    int y = (int)((packed >> 10) & 0x7FF);
    int z = (int)(packed & 0x3FF);
    if (x >= 1024) x -= 2048;
    if (y >= 1024) y -= 2048;
    if (z >= 512) z -= 1024;
    out.x = x * (3.14159265f / 1024.0f);
    out.y = y * (3.14159265f / 1024.0f);
    out.z = z * (3.14159265f / 512.0f);
}

#endif // VECTOR_MATRIX34_H
