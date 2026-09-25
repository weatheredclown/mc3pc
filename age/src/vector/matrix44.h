////////////////////////////////////////
// matrix44.h
////////////////////////////////////////

#ifndef VECTOR_MATRIX44_H
#define VECTOR_MATRIX44_H

#include "vector/Vector4.h"

class Matrix44
{
public:
    static const Matrix44 I;
    union {
        struct {
            Vector4 a;
            Vector4 b;
            Vector4 c;
            Vector4 d;
        };
        float m[16];
    };

    Matrix44() {}
    Matrix44(const class Matrix34 &m) { FromMatrix34(m); }
    Matrix44& operator=(const class Matrix34 &m) { FromMatrix34(m); return *this; }
    Matrix44(float m0, float m1, float m2, float m3,
             float m4, float m5, float m6, float m7,
             float m8, float m9, float m10, float m11,
             float m12, float m13, float m14, float m15) {
        m[0] = m0;   m[1] = m1;   m[2] = m2;   m[3] = m3;
        m[4] = m4;   m[5] = m5;   m[6] = m6;   m[7] = m7;
        m[8] = m8;   m[9] = m9;   m[10] = m10; m[11] = m11;
        m[12] = m12; m[13] = m13; m[14] = m14; m[15] = m15;
    }
    void Set(const Matrix44 &other) { *this = other; }
    void Set(const class Matrix34 &other) { FromMatrix34(other); }
    void Identity()
    {
        for(int i=0;i<16;i++) m[i]=0.0f;
        m[0]=m[5]=m[10]=m[15]=1.0f;
    }

    void MakeScale(float s) { Identity(); m[0] = m[5] = m[10] = s; }
    void MakeScale(float sx, float sy, float sz) { Identity(); m[0] = sx; m[5] = sy; m[10] = sz; }
    void MakeScaleFull(float s) { MakeScale(s); }
    void MakeScaleFull(float sx, float sy, float sz) { MakeScale(sx, sy, sz); }

    void MakeRotateX(float angle) {
        Identity();
        float c = cosf(angle), s = sinf(angle);
        m[5] = c; m[6] = s;
        m[9] = -s; m[10] = c;
    }
    void MakeRotateY(float angle) {
        Identity();
        float c = cosf(angle), s = sinf(angle);
        m[0] = c; m[2] = -s;
        m[8] = s; m[10] = c;
    }
    void MakeRotateZ(float angle) {
        Identity();
        float c = cosf(angle), s = sinf(angle);
        m[0] = c; m[1] = s;
        m[4] = -s; m[5] = c;
    }
    void MakeRotX(float angle) { MakeRotateX(angle); }
    void MakeRotY(float angle) { MakeRotateY(angle); }
    void MakeRotZ(float angle) { MakeRotateZ(angle); }

    void FromMatrix34(const class Matrix34 &mat);
    void ToMatrix34(class Matrix34 &out) const;
    void FastInverse(const class Matrix34 &mat);
    void FastInverse(const Matrix44 &mat) { Identity(); }

    void Dot(const Matrix44 &x, const Matrix44 &y)
    {
        Matrix44 r;
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                r.m[i*4 + j] = 0.0f;
                for (int k = 0; k < 4; k++) {
                    r.m[i*4 + j] += x.m[i*4 + k] * y.m[k*4 + j];
                }
            }
        }
        *this = r;
    }

    void Dot(const class Matrix34 &x, const class Matrix34 &y)
    {
        Matrix44 m1(x), m2(y);
        Dot(m1, m2);
    }

    void Dot(const Matrix44 &other)
    {
        Matrix44 temp = *this;
        Dot(temp, other);
    }

    void Transform(const Vector4 &in, Vector4 &out) const
    {
        out.x = in.x * m[0] + in.y * m[4] + in.z * m[8] + in.w * m[12];
        out.y = in.x * m[1] + in.y * m[5] + in.z * m[9] + in.w * m[13];
        out.z = in.x * m[2] + in.y * m[6] + in.z * m[10] + in.w * m[14];
        out.w = in.x * m[3] + in.y * m[7] + in.z * m[11] + in.w * m[15];
    }

    void Transform(const class Vector3 &in, class Vector3 &out) const;
};

#endif // VECTOR_MATRIX44_H
