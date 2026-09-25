////////////////////////////////////////
// polarcam.h
////////////////////////////////////////

#ifndef DEVCAM_POLARCAM_H
#define DEVCAM_POLARCAM_H

#include "vector/vector3.h"
#include "vector/Matrix34.h"

class bkBank;

////////////////////////////////////////////////////////////////////////////
// devPolarCam — developer orbit camera: the eye orbits a follow point at a
// given distance / azimuth / incline and looks back at it.  Left-drag orbits,
// the wheel dollies in/out.  Behaviour mirrors the original rb camDebugPolar
// (rb/src/camera/polarcam.cpp): positive incline lifts the eye above the
// subject.  The world matrix uses AGE's camera convention (a=right, b=up,
// c=forward toward the target, d=eye), matching Matrix34::LookAt.
////////////////////////////////////////////////////////////////////////////

class devPolarCam
{
public:
	devPolarCam();

	void Init(float dist, float az = 0.0f, float el = 0.0f);
	void SetLinearRate(float rate)				{ m_Rate = rate; }
	void SetFollow(const Vector3 &pos);
	void SetOffset(const Vector3 &offset);
	void Update();
	void Update(float dt)						{ Update(); }
	void SetDist(float dist)					{ m_Dist = dist; Recompute(); }
	float GetDist() const						{ return m_Dist; }
	void SetAzimuth(float val);
	void SetIncline(float val);
	float GetAzimuth() const					{ return m_Azimuth; }
	float GetIncline() const					{ return m_Elevation; }
	void AddWidgets(bkBank &bank);
	const Matrix34 &GetWorldMtx() const			{ return WorldMtx; }
	const Matrix34 &GetWorldMatrix() const		{ return WorldMtx; }

	Matrix34 WorldMtx;

private:
	void Recompute();
	void ClampAndRecompute();

	Vector3 m_Follow;
	Vector3 m_Offset;
	float   m_Dist;
	float   m_Azimuth;
	float   m_Elevation;   // "incline" in the original
	float   m_Rate;        // mouse orbit sensitivity (radians/pixel)
};

#endif // DEVCAM_POLARCAM_H
