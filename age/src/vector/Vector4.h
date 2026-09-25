////////////////////////////////////////
// Vector4.h
////////////////////////////////////////

#ifndef VECTOR_VECTOR4_H
#define VECTOR_VECTOR4_H

#ifndef VECTOR3
#define VECTOR3(v) (*(class Vector3*)&(v))
#endif

#include "vector/vector2.h"
#include "vector/vector3.h"

class Vector4 {
public:
    float x, y, z, w;

    Vector4() : x(0.0f), y(0.0f), z(0.0f), w(0.0f) {}
    Vector4(float _x, float _y, float _z, float _w) : x(_x), y(_y), z(_z), w(_w) {}
    Vector4(float _x, float _y, float _z) : x(_x), y(_y), z(_z), w(1.0f) {}
    Vector4(const Vector3 &v) : x(v.x), y(v.y), z(v.z), w(1.0f) {}

    float &operator[](int i) { return (&x)[i]; }
    const float &operator[](int i) const { return (&x)[i]; }

    void Multiply(const Vector4 &v) {
        x *= v.x; y *= v.y; z *= v.z; w *= v.w;
    }
    void Multiply(const Vector4 &a, const Vector4 &b) {
        x = a.x * b.x; y = a.y * b.y; z = a.z * b.z; w = a.w * b.w;
    }

    void AddVector3(const Vector3 &v) { x += v.x; y += v.y; z += v.z; }   // xyz += v
    void Add(const Vector4 &v) {
        x += v.x; y += v.y; z += v.z; w += v.w;
    }
    void Add(const Vector4 &a, const Vector4 &b) {
        x = a.x + b.x; y = a.y + b.y; z = a.z + b.z; w = a.w + b.w;
    }
    void Sub(const Vector4 &a, const Vector4 &b) {
        x = a.x - b.x; y = a.y - b.y; z = a.z - b.z; w = a.w - b.w;
    }

    float Dot(const Vector4 &v) const {
        return x * v.x + y * v.y + z * v.z + w * v.w;
    }

    void Set(float _x, float _y, float _z, float _w) {
        x = _x; y = _y; z = _z; w = _w;
    }
    void Set(float val) {
        x = y = z = w = val;
    }

    void Subtract(const Vector4 &v) { x -= v.x; y -= v.y; z -= v.z; w -= v.w; }
    void Subtract(const Vector4 &a, const Vector4 &b) { x = a.x - b.x; y = a.y - b.y; z = a.z - b.z; w = a.w - b.w; }

    void Set(float _x, float _y, float _z) {
        x = _x; y = _y; z = _z; w = 1.0f;
    }

    bool IsZero() const { return x == 0.0f && y == 0.0f && z == 0.0f && w == 0.0f; }
    bool IsNonZero() const { return x != 0.0f || y != 0.0f || z != 0.0f || w != 0.0f; }

    float Mag() const { return sqrtf(x*x + y*y + z*z + w*w); }

    void Normalize() {
        float d = x*x + y*y + z*z + w*w;
        if (d > 0.0f) {
            d = 1.0f / sqrtf(d);
            x *= d; y *= d; z *= d; w *= d;
        }
    }

    void Set(const Vector4 &v) {
        x = v.x; y = v.y; z = v.z; w = v.w;
    }

    void Set(const Vector3 &v, float _w = 1.0f) {
        x = v.x; y = v.y; z = v.z; w = _w;
    }

    void SetVector3(const Vector3 &v) {
        x = v.x; y = v.y; z = v.z;
    }

    void Zero() {
        x = 0.0f; y = 0.0f; z = 0.0f; w = 0.0f;
    }

    void Lerp(float t, const Vector4 &a, const Vector4 &b) {
        x = a.x + t * (b.x - a.x);
        y = a.y + t * (b.y - a.y);
        z = a.z + t * (b.z - a.z);
        w = a.w + t * (b.w - a.w);
    }

    float DistanceToPlane(const Vector3 &point) const {
        return x * point.x + y * point.y + z * point.z + w;
    }

    void ComputePlane(const Vector3 &point, const Vector3 &normal) {
        x = normal.x;
        y = normal.y;
        z = normal.z;
        w = -point.Dot(normal);
    }

    void ComputePlane(const Vector3 &p0, const Vector3 &p1, const Vector3 &p2) {
        Vector3 normal;
        normal.Cross(p1 - p0, p2 - p0);
        normal.Normalize();
        x = normal.x;
        y = normal.y;
        z = normal.z;
        w = -p0.Dot(normal);
    }

    float Mag2() const { return x*x + y*y + z*z; }

    float Dot3(const Vector3 &v) const { return x*v.x + y*v.y + z*v.z; }
    float Dot3(const Vector4 &v) const { return x*v.x + y*v.y + z*v.z; }

    void Scale(float s) {
        x *= s; y *= s; z *= s; w *= s;
    }
    void Scale(const Vector4 &v, float s) {
        x = v.x * s; y = v.y * s; z = v.z * s; w = v.w * s;
    }
    void Scale(const Vector3 &v, float s) {
        x = v.x * s; y = v.y * s; z = v.z * s; w = s;
    }
    void Scale3(float s) {
        x *= s; y *= s; z *= s;
    }
    void Scale3(const Vector4 &v, float s) {
        x = v.x * s; y = v.y * s; z = v.z * s;
    }
    Vector3 GetVector3() const {
        return Vector3(x, y, z);
    }
    void GetVector3(Vector3 &out) const {
        out.x = x; out.y = y; out.z = z;
    }
};

#endif // VECTOR_VECTOR4_H
