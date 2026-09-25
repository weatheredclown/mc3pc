#include "core/output.h"
////////////////////////////////////////
// vector2.h
////////////////////////////////////////

#ifndef VECTOR_VECTOR2_H
#define VECTOR_VECTOR2_H

class Vector2 {
public:
    float x, y;

    Vector2() : x(0.0f), y(0.0f) {}
    Vector2(float _x, float _y) : x(_x), y(_y) {}

    void Set(float _x, float _y) { x = _x; y = _y; }
    void Set(float val) { x = y = val; }
    void Set(const Vector2 &v) { x = v.x; y = v.y; }
    void Zero() { x = y = 0.0f; }
    bool IsZero() const { return x == 0.0f && y == 0.0f; }

    Vector2 operator-(const Vector2 &v) const { return Vector2(x - v.x, y - v.y); }
    Vector2 operator+(const Vector2 &v) const { return Vector2(x + v.x, y + v.y); }
    Vector2 operator*(float s) const { return Vector2(x * s, y * s); }
    void operator+=(const Vector2 &v) { x += v.x; y += v.y; }
    void operator-=(const Vector2 &v) { x -= v.x; y -= v.y; }
    void operator*=(float s) { x *= s; y *= s; }
    bool operator==(const Vector2 &v) const { return x == v.x && y == v.y; }
    bool operator!=(const Vector2 &v) const { return !(*this == v); }

    void Add(const Vector2 &v) { x += v.x; y += v.y; }
    void Add(const Vector2 &a, const Vector2 &b) { x = a.x + b.x; y = a.y + b.y; }
    void Sub(const Vector2 &v) { x -= v.x; y -= v.y; }
    void Sub(const Vector2 &a, const Vector2 &b) { x = a.x - b.x; y = a.y - b.y; }
    void Subtract(const Vector2 &v) { x -= v.x; y -= v.y; }
    void Subtract(const Vector2 &a, const Vector2 &b) { x = a.x - b.x; y = a.y - b.y; }
    void Negate() { x = -x; y = -y; }
    void Negate(const Vector2 &v) { x = -v.x; y = -v.y; }

    float Mag2() const { return x * x + y * y; }
    float Mag() const { return sqrtf(Mag2()); }

    void Lerp(float t, const Vector2 &a, const Vector2 &b) {
        x = a.x + t * (b.x - a.x);
        y = a.y + t * (b.y - a.y);
    }
    void AddScaled(const Vector2 &v, float s) { x += v.x * s; y += v.y * s; }
    void AddScaled(const Vector2 &a, const Vector2 &b, float s) {
        x = a.x + b.x * s;
        y = a.y + b.y * s;
    }
    float Dist(const Vector2 &v) const { return (*this - v).Mag(); }
    float Dist2(const Vector2 &v) const { return (*this - v).Mag2(); }
    bool IsClose(const Vector2 &v, float tolerance = 1e-4f) const { return Dist(v) < tolerance; }

    float Dot(const Vector2 &v) const { return x * v.x + y * v.y; }
    void Normalize() {
        float m = Mag();
        if (m > 0.0f) {
            x /= m; y /= m;
        }
    }
    void Scale(float s) { x *= s; y *= s; }
    void Scale(const Vector2 &v, float s) { x = v.x * s; y = v.y * s; }
    void SetScaled(const Vector2 &v, float s) { x = v.x * s; y = v.y * s; }
    void Print(const char *msg = nullptr) const { Quitf("Vector2::Print - not implemented"); }
    void Average(const Vector2 &a, const Vector2 &b) { x = (a.x + b.x) * 0.5f; y = (a.y + b.y) * 0.5f; }
    void Average(const Vector2 &v) { x = (x + v.x) * 0.5f; y = (y + v.y) * 0.5f; }

    void Min(const Vector2 &v) {
        if (v.x < x) x = v.x;
        if (v.y < y) y = v.y;
    }
    void Min(const Vector2 &a, const Vector2 &b) {
        x = (a.x < b.x) ? a.x : b.x;
        y = (a.y < b.y) ? a.y : b.y;
    }
    void Max(const Vector2 &v) {
        if (v.x > x) x = v.x;
        if (v.y > y) y = v.y;
    }
    void Max(const Vector2 &a, const Vector2 &b) {
        x = (a.x > b.x) ? a.x : b.x;
        y = (a.y > b.y) ? a.y : b.y;
    }
};

#endif // VECTOR_VECTOR2_H
