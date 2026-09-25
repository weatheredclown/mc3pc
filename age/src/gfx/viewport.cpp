#include "gfx/viewport.h"
#include "vector/Matrix34.h"
#include "gfx/rstate.h"
#include "data/args.h"
#include "bank/bank.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

gfxCullStatus gfxViewport::IsSphereVisible(float x, float y, float z, float radius, float *zDist) const {
    Vector3 worldPos(x, y, z);
    Vector3 camPos;
    RSTATE.GetCamera().UnTransform(worldPos, camPos);

    // AGE camera space: the matrix's c column is the boom (target back toward
    // the eye), so the camera looks down -c and on-screen points have NEGATIVE
    // z here; view depth is -z.  (rgl's SetCamera negates c alone to build the
    // D3D view -- same convention.)
    float depth = -camPos.z;
    if (zDist) {
        *zDist = depth;
    }

    // 1. Near/Far Plane Culling
    if (depth < m_Near - radius) {
        return cullOutside;
    }
    if (depth > m_Far + radius) {
        return cullOutside;
    }

    // 2. Lateral/Vertical Frustum Plane Culling
    float cosH, sinH, cosV, sinV;
    GetSidePlaneTrig(cosH, sinH, cosV, sinV);

    // Distances to the four side planes (normals point outside the frustum).
    // On the right plane x = depth*tanH = -z*tanH, so f = x*cosH + z*sinH.
    float dRight  = camPos.x * cosH + camPos.z * sinH;
    float dLeft   = -camPos.x * cosH + camPos.z * sinH;
    float dTop    = camPos.y * cosV + camPos.z * sinV;
    float dBottom = -camPos.y * cosV + camPos.z * sinV;

    if (dRight > radius || dLeft > radius || dTop > radius || dBottom > radius) {
        return cullOutside;
    }

    // Check if completely inside all planes
    if (depth - radius >= m_Near &&
        depth + radius <= m_Far &&
        dRight < -radius &&
        dLeft < -radius &&
        dTop < -radius &&
        dBottom < -radius) {
        return cullInside;
    }

    return cullClipped;
}

gfxCullStatus gfxViewport::IsAABBVisible(const Vector3 &min, const Vector3 &max, const Matrix34 &worldMtx, float *zDist) const {
    Vector3 localCorners[8] = {
        Vector3(min.x, min.y, min.z),
        Vector3(max.x, min.y, min.z),
        Vector3(min.x, max.y, min.z),
        Vector3(max.x, max.y, min.z),
        Vector3(min.x, min.y, max.z),
        Vector3(max.x, min.y, max.z),
        Vector3(min.x, max.y, max.z),
        Vector3(max.x, max.y, max.z)
    };

    const Matrix34 &cam = RSTATE.GetCamera();
    Vector3 camCorners[8];
    float minDepth = 1e30f;
    float maxDepth = -1e30f;

    for (int i = 0; i < 8; i++) {
        Vector3 worldPos;
        worldMtx.Transform(localCorners[i], worldPos);
        cam.UnTransform(worldPos, camCorners[i]);

        float depth = -camCorners[i].z;   // -z forward; see IsSphereVisible
        if (depth < minDepth) minDepth = depth;
        if (depth > maxDepth) maxDepth = depth;
    }

    if (zDist) {
        *zDist = (minDepth + maxDepth) * 0.5f;
    }

    // 1. Z Near/Far plane culling
    if (maxDepth < m_Near || minDepth > m_Far) {
        return cullOutside;
    }

    // If the AABB extends behind the near plane (camera inside or near the box),
    // unclipped corner side-plane math is invalid and the box is visible/clipped.
    if (minDepth < m_Near) {
        return cullClipped;
    }

    // 2. Lateral & Vertical frustum side planes
    float hfov = atanf(GetTanHFOV());
    float vfov = atanf(GetTanVFOV());
    float cosH = cosf(hfov);
    float sinH = sinf(hfov);
    float cosV = cosf(vfov);
    float sinV = sinf(vfov);

    bool outsideRight = true, outsideLeft = true, outsideTop = true, outsideBottom = true;
    for (int i = 0; i < 8; i++) {
        const Vector3 &cp = camCorners[i];
        float dR = cp.x * cosH + cp.z * sinH;
        float dL = -cp.x * cosH + cp.z * sinH;
        float dT = cp.y * cosV + cp.z * sinV;
        float dB = -cp.y * cosV + cp.z * sinV;

        if (dR <= 0.0f) outsideRight = false;
        if (dL <= 0.0f) outsideLeft = false;
        if (dT <= 0.0f) outsideTop = false;
        if (dB <= 0.0f) outsideBottom = false;
    }

    if (outsideRight || outsideLeft || outsideTop || outsideBottom) {
        return cullOutside;
    }

    return cullClipped;
}

gfxCullStatus gfxViewport::IsAABBVisible(const Vector3 &min, const Vector3 &max, float *zDist) const {
    Matrix34 identity;
    identity.Identity();
    return IsAABBVisible(min, max, identity, zDist);
}

float gfxViewport::GetProjectedPixelRadius(float radius, float zDist) const {
    float tanV = GetTanVFOV();
    if (zDist <= 0.0001f) zDist = 0.0001f;
    return (radius / (tanV * zDist)) * (m_Params.m_Height * 0.5f);
}

// The game's camera data tunes far clips (typically 1000) for PS2 rendering,
// where opaque distance fog hid the far plane.  The port draws no fog, so the
// naked clip plane slices through level vistas (e.g. the Blast Chambers ocean
// panorama).  Extend every perspective far clip to at least this floor;
// -farclip below overrides this.
// -farclip <units> given explicitly forces that exact far clip (diagnostic /
// user override); otherwise the game data's far is raised to a 10000 floor.
static bool sFarClipForced = false;
static float FarClipFloor() {
    static float sFloor = -1.0f;
    if (sFloor < 0.0f) {
        // The static pipeManager calls SetPerspective during static init,
        // before the ARGS singleton is constructed (cross-TU order) -- don't
        // latch a value until args exist and are populated.
        if (!args::sm_Instance || ARGS.Argc == 0)
            return 10000.0f;
        sFloor = 10000.0f;
        const char *fc = NULL;
        if (ARGS.Get("farclip", 0, &fc) && fc) {
            sFloor = (float)atof(fc);
            sFarClipForced = true;
        }
    }
    return sFloor;
}

void gfxViewport::SetPerspective(float fovYRadians, float zNear, float zFar) {
    m_Mode = projPerspective;
    // PC port: Guard against non-finite or out-of-range FOV
    if (!isfinite(fovYRadians) || fovYRadians < 0.0174533f) {
        fovYRadians = 0.0174533f; // ~1 deg
    } else if (fovYRadians > 3.054326f) {
        fovYRadians = 3.054326f; // ~175 deg
    }

    if (zFar != m_Far || zNear != m_Near) {
//        printf("[VIEWPORT %p] SetPerspective fov=%.1f near=%g far=%g\n",
//               (void *)this, fovYRadians * 57.2958f, zNear, zFar);
//        fflush(stdout);
    }
    if (zFar < FarClipFloor() || sFarClipForced)
        zFar = FarClipFloor();
    m_FovY = fovYRadians; m_Near = zNear; m_Far = zFar;
    float aspect = GetAspect();
    float f = 1.0f / tanf(fovYRadians * 0.5f);
    m_Proj.Identity();
    m_Proj.m[0] = f / aspect;
    m_Proj.m[5] = f;
    m_Proj.m[10] = zFar / (zFar - zNear);
    m_Proj.m[11] = 1.0f;
    m_Proj.m[14] = -(zNear * zFar) / (zFar - zNear);
    m_Proj.m[15] = 0.0f;
}

float gfxViewport::GetTanVFOV() const {
    return tanf(m_FovY * 0.5f);
}

float gfxViewport::GetTanHFOV() const {
    return GetTanVFOV() * GetAspect();
}

const Vector3 &gfxViewport::GetCameraPosition() const {
    return RSTATE.GetCamera().d;
}


// Both spheres outside the same frustum plane => the capsule between them is
// invisible.  Same camera-space plane set as IsSphereVisible.
bool gfxViewport::Fast2SpheresOutsideSinglePlane(const Vector3 &p1, float r1, const Vector3 &p2, float r2) const {
    Vector3 c1, c2;
    RSTATE.GetCamera().UnTransform(p1, c1);
    RSTATE.GetCamera().UnTransform(p2, c2);
    float d1 = -c1.z, d2 = -c2.z;
    if (d1 < m_Near - r1 && d2 < m_Near - r2) return true;
    if (d1 > m_Far + r1 && d2 > m_Far + r2) return true;

    float hfov = atanf(GetTanHFOV());
    float vfov = atanf(GetTanVFOV());
    float cosH = cosf(hfov), sinH = sinf(hfov);
    float cosV = cosf(vfov), sinV = sinf(vfov);
    if (c1.x * cosH + c1.z * sinH > r1 && c2.x * cosH + c2.z * sinH > r2) return true;     // right
    if (-c1.x * cosH + c1.z * sinH > r1 && -c2.x * cosH + c2.z * sinH > r2) return true;   // left
    if (c1.y * cosV + c1.z * sinV > r1 && c2.y * cosV + c2.z * sinV > r2) return true;     // top
    if (-c1.y * cosV + c1.z * sinV > r1 && -c2.y * cosV + c2.z * sinV > r2) return true;   // bottom
    return false;
}

bool gfxViewport::Fast2SpheresOutsideSinglePlane(const Vector4 &s1, const Vector4 &s2) const {
    return Fast2SpheresOutsideSinglePlane(Vector3(s1.x, s1.y, s1.z), s1.w, Vector3(s2.x, s2.y, s2.z), s2.w);
}

// Side-plane trig cache.  IsSphereVisible recomputes the atan/cos/sin of the
// half-FOVs on every call; the game asks for a prep once per frame and then
// culls thousands of spheres, so cache them keyed by FOV/aspect.
void gfxViewport::PrepFastSphereVisCheck() {
    float aspect = GetTanHFOV() / (GetTanVFOV() > 1e-6f ? GetTanVFOV() : 1e-6f);
    if (m_PrepFovY == m_FovY && m_PrepAspect == aspect)
        return;
    float hfov = atanf(GetTanHFOV());
    float vfov = atanf(GetTanVFOV());
    m_PrepCosH = cosf(hfov); m_PrepSinH = sinf(hfov);
    m_PrepCosV = cosf(vfov); m_PrepSinV = sinf(vfov);
    m_PrepFovY = m_FovY; m_PrepAspect = aspect;
}

void gfxViewport::GetSidePlaneTrig(float &cosH, float &sinH, float &cosV, float &sinV) const {
    float aspect = GetTanHFOV() / (GetTanVFOV() > 1e-6f ? GetTanVFOV() : 1e-6f);
    if (m_PrepFovY == m_FovY && m_PrepAspect == aspect) {
        cosH = m_PrepCosH; sinH = m_PrepSinH; cosV = m_PrepCosV; sinV = m_PrepSinV;
        return;
    }
    float hfov = atanf(GetTanHFOV());
    float vfov = atanf(GetTanVFOV());
    cosH = cosf(hfov); sinH = sinf(hfov); cosV = cosf(vfov); sinV = sinf(vfov);
}

// Bank widgets: the projection parameters and the window rect.  Edits to
// the rect take effect through SetWindow so the projection follows.
static void sViewportRectChanged(gfxViewport *vp) {
    const gfxViewportParams &p = vp->GetViewportParams();
    vp->SetWindow((int)p.m_X, (int)p.m_Y, (int)p.m_Width, (int)p.m_Height, p.m_MinZ, p.m_MaxZ);
}
static void sViewportProjChanged(gfxViewport *vp) {
    if (!vp->IsOrtho()) vp->SetPerspective(vp->GetFovY(), vp->GetNearClip(), vp->GetFarClip());
}

#if __BANK
void gfxViewport::AddWidgets(bkBank *bank) {
    if (!bank) return;
    bank->AddSlider("fov (rad)", &m_FovY, 0.0174533f, 3.054326f, 0.001f, datCallback(CFA1(sViewportProjChanged), this));
    bank->AddSlider("near clip", &m_Near, 0.001f, 100.0f, 0.001f, datCallback(CFA1(sViewportProjChanged), this));
    bank->AddSlider("far clip", &m_Far, 1.0f, 100000.0f, 1.0f, datCallback(CFA1(sViewportProjChanged), this));
    bank->AddSlider("window x", &m_Params.m_X, -4096.0f, 4096.0f, 1.0f, datCallback(CFA1(sViewportRectChanged), this));
    bank->AddSlider("window y", &m_Params.m_Y, -4096.0f, 4096.0f, 1.0f, datCallback(CFA1(sViewportRectChanged), this));
    bank->AddSlider("window width", &m_Params.m_Width, 1.0f, 8192.0f, 1.0f, datCallback(CFA1(sViewportRectChanged), this));
    bank->AddSlider("window height", &m_Params.m_Height, 1.0f, 8192.0f, 1.0f, datCallback(CFA1(sViewportRectChanged), this));
    bank->AddSlider("min z", &m_Params.m_MinZ, 0.0f, 1.0f, 0.01f, datCallback(CFA1(sViewportRectChanged), this));
    bank->AddSlider("max z", &m_Params.m_MaxZ, 0.0f, 1.0f, 0.01f, datCallback(CFA1(sViewportRectChanged), this));
}
#endif // __BANK
