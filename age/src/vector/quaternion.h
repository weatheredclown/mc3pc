#ifndef VECTOR_QUATERNION_H
#define VECTOR_QUATERNION_H

#include "core/output.h"
#include <math.h>

class Matrix34;

class Quaternion {
public:
    float x, y, z, w;

    Quaternion() : x(0.0f), y(0.0f), z(0.0f), w(1.0f) {}
    Quaternion(float _x, float _y, float _z, float _w) : x(_x), y(_y), z(_z), w(_w) {}

    void Set(float _x, float _y, float _z, float _w) { x = _x; y = _y; z = _z; w = _w; }
    void Set(const Quaternion &q) { x = q.x; y = q.y; z = q.z; w = q.w; }
    void Identity() { x = 0.0f; y = 0.0f; z = 0.0f; w = 1.0f; }
    void Zero() { x = 0.0f; y = 0.0f; z = 0.0f; w = 1.0f; }

    void FromMatrix34(const Matrix34 &m);
    void ToMatrix34(Matrix34 &m) const;
    void FromMatrix(const Matrix34 &m) { FromMatrix34(m); }
    void ToMatrix(Matrix34 &m) const { ToMatrix34(m); }

    float Dot(const Quaternion &q) const { return x * q.x + y * q.y + z * q.z + w * q.w; }
    void Negate() { x = -x; y = -y; z = -z; w = -w; }
    // Angle (radians) of the rotation taking this to q.
    float RelAngle(const Quaternion &q) const {
        float d = Dot(q); if (d < 0.0f) d = -d; if (d > 1.0f) d = 1.0f;
        return 2.0f * acosf(d);
    }
    void PrepareSlerp(const Quaternion &q) { Quitf("Quaternion::PrepareSlerp - not implemented"); }

    void Slerp(float t, const Quaternion &q1, const Quaternion &q2) {
        float cosHalfTheta = q1.w * q2.w + q1.x * q2.x + q1.y * q2.y + q1.z * q2.z;
        Quaternion target = q2;
        if (cosHalfTheta < 0.0f) {
            cosHalfTheta = -cosHalfTheta;
            target.w = -target.w;
            target.x = -target.x;
            target.y = -target.y;
            target.z = -target.z;
        }
        if (fabsf(cosHalfTheta) >= 1.0f) {
            w = q1.w; x = q1.x; y = q1.y; z = q1.z;
            return;
        }
        float halfTheta = acosf(cosHalfTheta);
        float sinHalfTheta = sqrtf(1.0f - cosHalfTheta * cosHalfTheta);
        if (fabsf(sinHalfTheta) < 0.001f) {
            w = q1.w * 0.5f + target.w * 0.5f;
            x = q1.x * 0.5f + target.x * 0.5f;
            y = q1.y * 0.5f + target.y * 0.5f;
            z = q1.z * 0.5f + target.z * 0.5f;
            return;
        }
        float ratioA = sinf((1.0f - t) * halfTheta) / sinHalfTheta;
        float ratioB = sinf(t * halfTheta) / sinHalfTheta;
        w = q1.w * ratioA + target.w * ratioB;
        x = q1.x * ratioA + target.x * ratioB;
        y = q1.y * ratioA + target.y * ratioB;
        z = q1.z * ratioA + target.z * ratioB;
    }
};

#endif // VECTOR_QUATERNION_H
