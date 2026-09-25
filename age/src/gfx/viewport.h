#include "vector/Vector4.h"
////////////////////////////////////////
// viewport.h
//
// A viewport: the rectangle we render into plus the projection parameters
// (field of view, near/far clip).  pipeManager owns the main perspective
// viewport (VP) and an ortho viewport (OrthoVP), sizing them from the window.
////////////////////////////////////////

#ifndef GFX_VIEWPORT_H
#define GFX_VIEWPORT_H

#include "core/types.h"
#include "vector/matrix44.h"

enum gfxCullStatus {
    cullOutside = 0,
    cullInside,
    cullClipped,
    cullIntersect,

    cullOut = cullOutside,
    cullIn = cullInside
};

struct gfxViewportParams {
    float m_Width;
    float m_Height;
    float m_X;
    float m_Y;
    float m_MinZ;   // depth range of the window (0..1)
    float m_MaxZ;
};

class gfxViewport {
public:
    gfxViewport()
        : m_Near(0.1f), m_Far(1000.0f), m_FovY(60.0f * 3.14159265f / 180.0f) {
        m_Params.m_Width  = 640.0f;
        m_Params.m_Height = 480.0f;
        m_Params.m_X = 0.0f;
        m_Params.m_Y = 0.0f;
        m_Params.m_MinZ = 0.0f;
        m_Params.m_MaxZ = 1.0f;
        SetPerspective(m_FovY, m_Near, m_Far);
    }
    virtual ~gfxViewport() {}

    // Bank widgets: field of view, clip planes and the window rect (viewport.cpp).
    void AddWidgets(class bkBank* bank);
    void AddWidgets(class bkBank& bank) { AddWidgets(&bank); }

    // Projection mode.  A viewport is either perspective, "screen ortho"
    // (OrthoScreen: absolute screen pixels over its window, so moving the
    // window scissors rather than rescales -- the menus' clipping trick) or
    // an explicit Ortho2D/Ortho range mapped into the window (HUD map, env
    // map targets).  SetWindow re-derives the first two from the new rect.
    enum ProjMode { projPerspective = 0, projOrthoScreen = 1, projOrtho = 2 };
    bool IsOrtho() const { return m_Mode != projPerspective; }
    ProjMode GetProjMode() const { return m_Mode; }

    // --- setup (driven by pipeManager) ---
    void SetRect(float w, float h, float x = 0.0f, float y = 0.0f) {
        m_Params.m_Width = w; m_Params.m_Height = h;
        m_Params.m_X = x;     m_Params.m_Y = y;
    }
    void SetPerspective(float fovYRadians, float zNear, float zFar);
    void Release() { delete this; }
    // Window rect in screen pixels + depth range; the projection follows the
    // new rect (aspect for perspective, absolute pixel range for screen ortho).
    void SetWindow(int x, int y, int w, int h, float minZ = 0.0f, float maxZ = 1.0f) {
        SetRect((float)w, (float)h, (float)x, (float)y);
        m_Params.m_MinZ = minZ; m_Params.m_MaxZ = maxZ;
        if (m_Mode == projOrthoScreen) BuildOrthoScreen();
        else if (m_Mode == projPerspective) SetPerspective(m_FovY, m_Near, m_Far);
    }
    void SetNearClip(float nearClip) { m_Near = nearClip; if (m_Mode == projPerspective) SetPerspective(m_FovY, m_Near, m_Far); }
    void Perspective(float fovYDegrees, float aspect, float zNear, float zFar) { (void)aspect; SetPerspective(fovYDegrees * 3.14159265f / 180.0f, zNear, zFar); }
    // Explicit ortho range: [left,right] x [bottom,top] maps onto the window;
    // z passes through (draws at z = 0 sit mid-range).
    void Ortho2D(float left, float right, float bottom, float top) {
        m_Mode = projOrtho;
        BuildOrtho(left, right, bottom, top, 1.0f, 0.0f);
    }
    // Absolute screen pixels over the window (see ProjMode).
    void OrthoScreen() {
        m_Mode = projOrthoScreen;
        BuildOrthoScreen();
    }

    // --- queries ---
    float GetZClipNear() const { return m_Near; }
    float GetZClipFar() const  { return m_Far; }
    float GetNearClip() const { return m_Near; }
    float GetFarClip() const { return m_Far; }
    void  SetFarClip(float farClip) { m_Far = farClip; }
    float GetYFOV() const { return m_FovY; }
    float GetFovY() const { return m_FovY; }
    float GetFOV() const { return m_FovY; }

    float GetAspect() const {
        return (m_Params.m_Height > 0.0f) ? (m_Params.m_Width / m_Params.m_Height) : (4.0f / 3.0f);
    }
    float GetAspectRatio() const { return GetAspect(); }
    float GetTanVFOV() const;
    float GetTanHFOV() const;
    float GetCullRadius() const { return m_Far; }

    const gfxViewportParams & GetViewportParams() const { return m_Params; }

    const class Matrix44 & GetProjection() const {
        return m_Proj;
    }
    void SetProjection(const class Matrix44 &proj) {
        m_Proj = proj;
    }
    // True when both spheres lie fully outside the SAME frustum plane, which
    // proves the convex hull between them (a capsule) is invisible.
    bool Fast2SpheresOutsideSinglePlane(const Vector3 &p1, float r1, const Vector3 &p2, float r2) const;
    bool Fast2SpheresOutsideSinglePlane(const class Vector4 &s1, const class Vector4 &s2) const;
    bool InlineFast2SpheresOutsideSinglePlane(const class Vector4 &s1, const class Vector4 &s2) const { return Fast2SpheresOutsideSinglePlane(s1, s2); }
    bool InlineFast2SpheresOutsideSinglePlane(const Vector3 &p1, float r1, const Vector3 &p2, float r2) const { return Fast2SpheresOutsideSinglePlane(p1, r1, p2, r2); }

    // Orthographic projection over [left,right] x [bottom,top] with depth mapped from zNear..zFar.
    void Ortho(float left, float right, float bottom, float top, float zNear, float zFar) {
        m_Mode = projOrtho;
        m_Near = zNear; m_Far = zFar;
        float range = (zFar - zNear) != 0.0f ? (zFar - zNear) : 1.0f;
        BuildOrtho(left, right, bottom, top, 1.0f / range, -zNear / range);
    }
    // Conservative frustum test: checks the sphere against the six camera-space planes,
    // and returns the real camera-space depth for LOD selection.
    gfxCullStatus IsSphereVisible(float x, float y, float z, float radius, float *zDist = NULL) const;

    // Caches the side-plane trig for the current FOV/aspect so the per-object
    // sphere checks below skip the atan/cos/sin.  Call once per frame after
    // the camera and projection are set (mc3 does so right after SetCamera).
    // The cache self-validates against FOV/aspect, so stale calls are safe.
    void PrepFastSphereVisCheck();

    gfxCullStatus FastSphereVisCheck(const Vector3 &center, float radius) const {
        return IsSphereVisible(center.x, center.y, center.z, radius);
    }
    gfxCullStatus FastSphereVisCheck(const Matrix34 &m, float radius) const {
        return IsSphereVisible(m.d.x, m.d.y, m.d.z, radius);
    }
    gfxCullStatus FastSphereVisCheck(const Vector4 &centerRadius) const {
        return IsSphereVisible(centerRadius.x, centerRadius.y, centerRadius.z, centerRadius.w);
    }
    gfxCullStatus FastSphereVisCheck(const Vector4 &centerRadius, float &zDist) const {
        return IsSphereVisible(centerRadius.x, centerRadius.y, centerRadius.z, centerRadius.w, &zDist);
    }
    // The zDist forms return the cull status (mc3 keeps it for LOD decisions);
    // cullOutside is 0 so they still read as booleans.
    gfxCullStatus InlineFastSphereVisCheck(const Vector4 &centerRadius, float &zDist) const {
        return IsSphereVisible(centerRadius.x, centerRadius.y, centerRadius.z, centerRadius.w, &zDist);
    }
    gfxCullStatus InlineFastSphereVisCheck(const Vector4 &centerRadius) const {
        return IsSphereVisible(centerRadius.x, centerRadius.y, centerRadius.z, centerRadius.w);
    }
    bool InlineFastSphereCullOutsideCheckLRN(const Vector4 &centerRadius) const {
        return IsSphereVisible(centerRadius.x, centerRadius.y, centerRadius.z, centerRadius.w) == cullOutside;
    }
    gfxCullStatus InlineFastSphereVisCheck(const Vector3 &center, float radius, float &zDist) const {
        return IsSphereVisible(center.x, center.y, center.z, radius, &zDist);
    }
    bool InlineFastSphereVisCheck(const Vector3 &center, float radius) const {
        return IsSphereVisible(center.x, center.y, center.z, radius) != cullOutside;
    }
    const Vector3 &GetCameraPosition() const;

    // AABB frustum test: checks world/camera space AABB corners against frustum planes.
    gfxCullStatus IsAABBVisible(const class Vector3 &min, const class Vector3 &max, const class Matrix34 &worldMtx, float *zDist = NULL) const;
    gfxCullStatus IsAABBVisible(const class Vector3 &min, const class Vector3 &max, float *zDist = NULL) const;

    // Returns projected screen-space size in pixels for a bounding sphere at camera-space distance zDist.
    float GetProjectedPixelRadius(float radius, float zDist) const;

private:
    // Side-plane trig cache (PrepFastSphereVisCheck); valid while the FOV
    // and aspect it was built for are unchanged.
    void GetSidePlaneTrig(float &cosH, float &sinH, float &cosV, float &sinV) const;

    // Row-vector D3D ortho: x/y ranges to clip [-1,1], z' = z * zScale + zBias.
    void BuildOrtho(float left, float right, float bottom, float top, float zScale, float zBias) {
        if (right == left || top == bottom) { right = left + 1.0f; top = bottom - 1.0f; }   // degenerate ortho (unsized viewport)
        m_Proj.Identity();
        m_Proj.m[0] = 2.0f / (right - left);
        m_Proj.m[5] = 2.0f / (top - bottom);
        m_Proj.m[10] = zScale;
        m_Proj.m[12] = -(right + left) / (right - left);
        m_Proj.m[13] = -(top + bottom) / (top - bottom);
        m_Proj.m[14] = zBias;
        m_Proj.m[15] = 1.0f;
    }
    // Screen ortho over the window rect, z in [-1,1] (the classic ortho
    // viewport's range, so 2D draws at any small z stay inside the clip volume).
    void BuildOrthoScreen() {
        const float x = m_Params.m_X, y = m_Params.m_Y;
        BuildOrtho(x, x + m_Params.m_Width, y + m_Params.m_Height, y, 0.5f, 0.5f);
    }

    ProjMode m_Mode = projPerspective;
    gfxViewportParams m_Params;
    float m_Near;
    float m_Far;
    float m_FovY;   // vertical field of view, radians
    Matrix44 m_Proj;

    float m_PrepFovY = -1.0f;
    float m_PrepAspect = -1.0f;
    float m_PrepCosH = 1.0f, m_PrepSinH = 0.0f, m_PrepCosV = 1.0f, m_PrepSinV = 0.0f;
};

#endif // GFX_VIEWPORT_H
