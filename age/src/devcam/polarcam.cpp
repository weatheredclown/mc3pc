////////////////////////////////////////
// polarcam.cpp
//
// devPolarCam — developer orbit camera.  Orbit math mirrors the original rb
// camDebugPolar (rb/src/camera/polarcam.cpp): the eye sits on a sphere of
// radius Distance about the follow point at (Azimuth, Incline), positive
// Incline lifting the eye above the subject.  The original drove it from the
// gamepad + keyboard; this AGE debug build OVERLAYS mouse control (left-drag
// orbits, wheel dollies) on top of a port of those keyboard bindings, since a
// tester is mouse-first.
//
// Input is read self-contained (absolute mouse position + cumulative wheel,
// diffed internally) so it stays stable even though the testers don't reset the
// per-frame input deltas (ioMouse::ClearEdges) each frame.
////////////////////////////////////////

#include "devcam/polarcam.h"
#include "input/mouse.h"
#include "input/keyboard.h"
#include "input/keys.h"
#include "data/timemgr.h"

#include <math.h>

devPolarCam::devPolarCam()
	: m_Dist(4.0f), m_Azimuth(0.0f), m_Elevation(0.0f), m_Rate(0.01f)
{
	m_Follow.Zero();
	m_Offset.Zero();
	Recompute();
}

void devPolarCam::Init(float dist, float az, float el)
{
	m_Dist = dist; m_Azimuth = az; m_Elevation = el;
	m_Follow.Zero(); m_Offset.Zero();
	Recompute();
}

void devPolarCam::SetFollow(const Vector3 &pos)		{ m_Follow = pos; Recompute(); }
void devPolarCam::SetOffset(const Vector3 &offset)	{ m_Offset = offset; Recompute(); }
void devPolarCam::SetAzimuth(float val)				{ m_Azimuth = val; Recompute(); }
void devPolarCam::SetIncline(float val)				{ m_Elevation = val; ClampAndRecompute(); }

void devPolarCam::Update()
{
	// --- Mouse (primary): left-drag orbits, wheel dollies.  ioMouse deltas are
	//     per-frame (reset in pipeManager::EndFrame -> ClearEdges). ---
	if (ioMouse::GetButtons() & ioMouse::mouseLeft)
	{
		m_Azimuth   += ioMouse::GetDX() * m_Rate;
		m_Elevation += ioMouse::GetDY() * m_Rate;
	}
	int dWheel = ioMouse::GetDZ();
	if (dWheel) m_Dist *= powf(0.9f, (float)dWheel);   // wheel-up (positive) zooms in

	// --- Keyboard overlay (port of rb camDebugPolar bindings) ---
	float dt = TIME.GetSeconds();
	if (dt <= 0.0f) dt = 1.0f / 60.0f;
	bool fast = ioKeyboard::KeyDown(KEY_CONTROL);
	float ang = (fast ? 4.0f : 2.0f) * dt;   // AngularRate (rb: 2.0, x2 with Ctrl)
	float lin = (fast ? 10.0f : 1.0f) * dt;  // LinearRate  (rb: 1.0, x10 with Ctrl)
	lin *= m_Dist;                            // scale zoom by distance for a usable feel

	if (ioKeyboard::KeyDown(KEY_DELETE) ||
	    ioKeyboard::KeyDown(KEY_LEFT))     m_Azimuth   -= ang;   // AziLeft
	if (ioKeyboard::KeyDown(KEY_PAGEDOWN) ||
	    ioKeyboard::KeyDown(KEY_RIGHT))    m_Azimuth   += ang;   // AziRight
	if (ioKeyboard::KeyDown(KEY_HOME) ||
	    ioKeyboard::KeyDown(KEY_UP))       m_Elevation += ang;   // IncUp
	if (ioKeyboard::KeyDown(KEY_END) ||
	    ioKeyboard::KeyDown(KEY_DOWN))     m_Elevation -= ang;   // IncDown
	if (ioKeyboard::KeyDown(KEY_PAGEUP))   m_Dist       -= lin;  // ZoomIn
	if (ioKeyboard::KeyDown(KEY_INSERT))   m_Dist       += lin;  // ZoomOut

	ClampAndRecompute();
}

void devPolarCam::ClampAndRecompute()
{
	const float kElevLimit = 1.55f;   // ~89 degrees; avoids the straight-up/down gimbal
	if (m_Elevation >  kElevLimit) m_Elevation =  kElevLimit;
	if (m_Elevation < -kElevLimit) m_Elevation = -kElevLimit;
	if (m_Dist < 0.1f) m_Dist = 0.1f;
	Recompute();
}

void devPolarCam::Recompute()
{
	// Unit vector from the target toward the eye.  az=el=0 places the eye at -Z
	// looking +Z (same framing as the old devTrackCam default); positive incline
	// lifts the eye above the subject, matching camDebugPolar.
	float ce = cosf(m_Elevation), se = sinf(m_Elevation);
	Vector3 toEye(sinf(m_Azimuth) * ce, se, -cosf(m_Azimuth) * ce);

	Vector3 target = m_Follow;
	target.Add(m_Offset);

	Vector3 eye;
	eye.x = target.x + toEye.x * m_Dist;
	eye.y = target.y + toEye.y * m_Dist;
	eye.z = target.z + toEye.z * m_Dist;

	WorldMtx.LookAt(eye, target);
}

#if __BANK
#include "bank/bank.h"
void devPolarCam::AddWidgets(bkBank &bank)
{
	bank.AddSlider("Distance", &m_Dist, 0.1f, 1000.0f, 0.1f);
	bank.AddSlider("Azimuth",  &m_Azimuth, -6.2832f, 6.2832f, 0.01f);
	bank.AddSlider("Incline",  &m_Elevation, -1.55f, 1.55f, 0.01f);
}
#else
void devPolarCam::AddWidgets(bkBank &) {}
#endif
