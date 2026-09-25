////////////////////////////////////////
// Matrix34.cpp
////////////////////////////////////////

#include "vector/Matrix34.h"

static Matrix34 InitIdentity() {
    Matrix34 m;
    m.a.Set(1.0f, 0.0f, 0.0f);
    m.b.Set(0.0f, 1.0f, 0.0f);
    m.c.Set(0.0f, 0.0f, 1.0f);
    m.d.Set(0.0f, 0.0f, 0.0f);
    return m;
}

const Matrix34 Matrix34::I = InitIdentity();

static Matrix44 InitIdentity44() {
    Matrix44 m;
    m.Identity();
    return m;
}

const Matrix44 Matrix44::I = InitIdentity44();

const Vector3 ORIGIN(0.0f, 0.0f, 0.0f);
const Vector3 XAXIS(1.0f, 0.0f, 0.0f);
const Vector3 YAXIS(0.0f, 1.0f, 0.0f);
const Vector3 ZAXIS(0.0f, 0.0f, 1.0f);

void Matrix44::Transform(const Vector3 &in, Vector3 &out) const
{
    Vector4 tempIn(in.x, in.y, in.z, 1.0f);
    Vector4 tempOut;
    Transform(tempIn, tempOut);
    if (tempOut.w != 0.0f) {
        float invW = 1.0f / tempOut.w;
        out.x = tempOut.x * invW;
        out.y = tempOut.y * invW;
        out.z = tempOut.z * invW;
    } else {
        out.x = tempOut.x;
        out.y = tempOut.y;
        out.z = tempOut.z;
    }
}

////////////////////////////////////////////////////////////////////////////
// Euler angles, arbitrary axis order.
//
// Row-vector convention: a point transforms as v * M and Dot(x,y) = x*y, so
// for an order string "ijk" the matrix is  M = R_i * R_j * R_k  (rotate about
// i first, then j, then k, each about the parent axis).  This is exactly how
// FromEulersXYZ (Rx*Ry*Rz) and FromEulersXZY (Rx*Rz*Ry, transcribed from the
// original frame.cpp) are laid out, so those stay the fast paths and the
// generic code here only has to agree with them.
//
// Extraction: with e = +1 for an even permutation of xyz and -1 for odd,
//     M[i][k]           = -e * sin(j)
//     atan2(e*M[j][k], M[k][k])  -> angle i
//     atan2(e*M[i][j], M[i][i])  -> angle k
// and at gimbal lock (cos(j) ~ 0) angle k is set to 0 and
//     atan2(-e*M[k][j], M[j][j]) -> angle i.
////////////////////////////////////////////////////////////////////////////

static bool sEulerOrder(const char *order, int &i, int &j, int &k, float &sign)
{
	if (!order || !order[0] || !order[1] || !order[2])
		return false;
	int idx[3];
	for (int n = 0; n < 3; n++)
	{
		char ch = order[n];
		if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
		if (ch < 'x' || ch > 'z') return false;
		idx[n] = ch - 'x';
	}
	if (idx[0] == idx[1] || idx[1] == idx[2] || idx[0] == idx[2])
		return false;								// proper Euler (xyx etc) unsupported
	i = idx[0]; j = idx[1]; k = idx[2];
	// (0,1,2),(1,2,0),(2,0,1) are the even permutations.
	sign = ((j - i + 3) % 3 == 1) ? 1.0f : -1.0f;
	return true;
}

// Row-vector rotation about a single axis index (0=x,1=y,2=z), same
// handedness as MakeRotateX/Y and Vector3::RotateX/Y/Z.
static void sAxisRotate(Matrix34 &m, int axis, float ang)
{
	float s = sinf(ang), c = cosf(ang);
	m.Identity3x3();
	int u = (axis + 1) % 3, v = (axis + 2) % 3;
	Vector3 *rows[3] = { &m.a, &m.b, &m.c };
	(*rows[u])[u] = c;  (*rows[u])[v] = s;
	(*rows[v])[u] = -s; (*rows[v])[v] = c;
}

Vector3 Matrix34::GetEulers(const char *order) const
{
	int i, j, k; float e;
	if (!sEulerOrder(order, i, j, k, e))
		return Vector3(0.0f, 0.0f, 0.0f);

	const Vector3 *rows[3] = { &a, &b, &c };
	#define M(r, col) ((*rows[r])[col])

	float sj = -e * M(i, k);
	if (sj > 1.0f) sj = 1.0f; else if (sj < -1.0f) sj = -1.0f;
	float angJ = asinf(sj);
	float cj = cosf(angJ);

	float angI, angK;
	if (cj > 1e-5f)
	{
		angI = atan2f(e * M(j, k), M(k, k));
		angK = atan2f(e * M(i, j), M(i, i));
	}
	else
	{
		angK = 0.0f;
		angI = atan2f(-e * M(k, j), M(j, j));
	}
	#undef M

	Vector3 out;
	out[i] = angI; out[j] = angJ; out[k] = angK;
	return out;
}

void Matrix34::FromEulers(const Vector3 &eul, const char *order)
{
	int i, j, k; float e;
	if (!sEulerOrder(order, i, j, k, e))
	{
		FromEulersXZY(eul);						// engine default order
		return;
	}
	if (i == 0 && j == 1 && k == 2) { FromEulersXYZ(eul); return; }
	if (i == 0 && j == 2 && k == 1) { FromEulersXZY(eul); return; }
	Matrix34 ri, rj, rk;
	sAxisRotate(ri, i, eul[i]);
	sAxisRotate(rj, j, eul[j]);
	sAxisRotate(rk, k, eul[k]);
	Matrix34 r;
	r.Dot3x3(ri, rj);
	Dot3x3(r, rk);								// 3x3 only: d preserved, like Make*
}

void Matrix34::FromEulersZYX(const Vector3 &e)
{
	FromEulers(e, "zyx");
}
