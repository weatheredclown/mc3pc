////////////////////////////////////////
// simple.h
////////////////////////////////////////

#ifndef GFX_SIMPLE_H
#define GFX_SIMPLE_H

#include "core/output.h"
#include "core/types.h"
#include "data/main.h"

// Simple-gfx umbrella: immediate-mode geometry (rgl/vgl) + render state (RSTATE,
// MTX, lights/materials), alongside the pipe/viewport managers.
#include "gfx/vgl.h"
#include "gfx/rstate.h"
#include "gfx/viewport.h"
#include "gfx/texture.h"
#include "gfx/font.h"
#include "gfx/vglext.h"

// The graphics pipeline manager (PIPE).  Owns the window/device lifecycle, the
// per-frame begin/clear/end cycle, message pumping, the current resolution, and
// the perspective/ortho viewports.  This is the single place the AGE render
// boilerplate lives; the age* globals (ageBeginGfx/ageBeginFrame/ageEndFrame/
// ageEndGfx) and the rb shell both drive it.
// rgl.cpp: 2D projection while the ortho viewport is current (SetViewport).
void vglSetViewportOrtho(bool ortho);

class pipeManager {
public:
    static pipeManager* sm_Instance;

    gfxViewport* VP;
    gfxViewport* OrthoVP;
    gfxViewport* m_DefaultVP;

    pipeManager();
    ~pipeManager();

    // --- window / device lifecycle ---
    void SetTitle(const char *title);
    // Run in a desktop window (centred on the primary monitor) rather than
    // full screen: the __DEV video config and the testers call it after
    // SetRes and before the device opens; it also drops an open device's
    // window back out of full screen.
    void SetWindow();
    // Resolution, colour depth, PS2 display flags and the full-screen request
    // (fs = borderless over the window's monitor once the device is open).
    void SetRes(int w = 640, int h = 480, int d = 16, int f = 0, bool fs = false);
    bool IsFullscreen() const { return m_Fullscreen; }
    void InitClass();
    void Begin();                       // create window + open device (if needed)
    void End();                         // close device + destroy window
    void ShutdownClass() { End(); }

    // --- per-frame ---
    void Manage();                      // pump window messages / input
    void BeginFrame();                  // begin frame (clears to ageClearColor)
    void Clear(u32 flags, u32 color);   // clear color+z
    void Clear(u32 flags) { Clear(flags, 0); }
    void EndFrame();                    // present
    void FlushFrame();                  // PS2 "display now": composes on PC (see simple.cpp)
    void ClearRect(int x, int y, int w, int h, u32 color);
    void ClearRect(int x, int y, int w, int h, const class Vector4 &color);
    // Redirect drawing into a texture (env maps, HUD readbacks); NULL restores
    // the backbuffer.  Front/back FB targets from CreateRenderTarget are markers,
    // so retargeting to one of those is a no-op.  This form binds colour only -
    // right for 2D (baked text, gauge faces), wrong for a 3D scene.
    void SetRenderTarget(class gfxTexture *tex, int arg2 = 0, int arg3 = 0);
    // AGE 2.72 form: colour target plus depth target.  A non-null zbuffer asks
    // for a real depth buffer, which is what a 3D pass drawn into a texture needs.
    void SetRenderTarget(class gfxTexture *tex, class gfxTexture *zbuffer);
    void CopyBitmap(int x, int y, class gfxBitmap *bmp);   // draw a bitmap 1:1 at (x,y) (loading screens, frontend pages)
    void Blit2D(float x1, float y1, float x2, float y2, float u1, float v1, float u2, float v2, u32 color);
    void Blit2D(float x1, float y1, float x2, float y2, float u1, float v1, float u2, float v2, u32 color, bool, bool);
    void Blit2D(float x1, float y1, float x2, float y2, float u1, float v1, float u2, float v2, u32 color, bool b) { Blit2D(x1, y1, x2, y2, u1, v1, u2, v2, color, b, false); }
    int GetCopyToFrontColorDepth() { return 32; }
    // Copy-to-front hook (fxCopyToFront): called by gfxEndFrame right before
    // the frame is captured/presented, with the backbuffer copy bound-able.
    void SetCopyToFrontFunc(void (*func)());
    // Vertical-blank lock the game runs at (1 = every vblank, 2 = every other); the
    // D3D11 present is vsync-paced so this only records the request.
    int GetFrameLock() const { return m_FrameLock; }
    // Vertical-blank lock (1 = every vblank, 2 = every other); presents are vsync-paced, so only recorded.
    void SetFrameLock(int lock, bool persist = false) { (void)persist; m_FrameLock = lock > 0 ? lock : 1; }
    u32 GetEventFlags() const { return m_EventFlags; }
    void ClearEventFlags(u32 flags) { m_EventFlags &= ~flags; }
    void SetEventFlags(u32 flags) { m_EventFlags |= flags; }
    void SetGamma(float exp1, float exp2, int flags = 0);
    // Off-screen render buffer the full-screen effects sample (mc3 mcConfig
    // creates it when those effects are enabled).  On this backend that is
    // the backbuffer copy the copy-to-front hook refreshes: Create allocates
    // it up front, Delete releases it (it is re-created on demand).
    void CreateRenderBuffer();
    void DeleteRenderBuffer();
    bool HasRenderBuffer() const;
    // The finished frame as a texture (copy refreshed from the swapchain).
    class gfxTexture *GetRenderFrontBuffer();

    // --- notifications from the app framework (ageBeginGfx/ageEndGfx) ---
    void OnDeviceOpened(int w, int h);
    void OnDeviceClosed();
    bool IsDeviceOpen() const { return m_DeviceOpen; }

    // --- widgets / viewports ---
    void AddWidgets(class bkBank* bank);
    void AddWidgets(class bkBank& bank) { AddWidgets(&bank); }

    // Selecting an ortho viewport (the pipeline's OrthoVP or any viewport
    // set up with OrthoScreen/Ortho2D) switches the immediate-mode backend
    // into screen-space projection; the viewport's window rect is applied
    // per draw by the backend.
    void SetViewport(gfxViewport *vp) { VP = vp ? vp : m_DefaultVP; vglSetViewportOrtho(VP && VP->IsOrtho()); }
    // New viewports start sized to the window with the default perspective, so
    // OrthoScreen()/Perspective() on them (HUD, LED panels) have real extents.
    gfxViewport *CreateViewport();
    gfxViewport* GetViewport() { return VP; }
    gfxViewport* GetOrthoViewport() { return OrthoVP; }

    // --- CPU readback (map snapshots, PVS builds) ---
    // CreateReadbackImage allocates an RGBA8 gfxImage sized to the current
    // backbuffer; Readback fills it from the backbuffer (w/h of 0 = whole
    // backbuffer).  Release() the image when done.
    class gfxImage *CreateReadbackImage();
    void Readback(class gfxImage *img, int x = 0, int y = 0, int w = 0, int h = 0);

    // Blit2D texel scale: PS2/D3D8 code passes texture coordinates in pixels
    // and sets (1/w, 1/h) here so the blit normalises them.  Default 1.0.
    void SetBlit2DTexScale(float su, float sv) { m_Blit2DTexScaleU = su; m_Blit2DTexScaleV = sv; }
    float GetBlit2DTexScaleU() const { return m_Blit2DTexScaleU; }
    float GetBlit2DTexScaleV() const { return m_Blit2DTexScaleV; }

    // Copy of the finished backbuffer as a bindable texture (fullscreen
    // post effects: glows/sun streaks).  Refreshed by the copy-to-front hook;
    // NULL until the first frame has been captured.
    class gfxTexture *GetRenderBackBuffer();

    // D3D8-era shader handles.  The game only ever passes NULL (fixed function)
    // or an FVF code (mc3 glows); the D3D11 backend has one vertex layout and
    // one pixel shader that cover every FVF the game uses, so the handles are
    // recorded for GetVertexShader-style readers and nothing else changes.
    void SetVertexShader(const void *shader) { m_VertexShader = shader; }
    void SetPixelShader(const void *shader) { m_PixelShader = shader; }
    const void *GetVertexShader() const { return m_VertexShader; }
    const void *GetPixelShader() const { return m_PixelShader; }

    int GetFrameNumber() const { return m_FrameNumber; }
    bool IsInFrame() const { return m_InFrame; }
    // Capture the next presented frame to `filename` (rgl -shot path).
    void RequestScreenShot(const char *filename = nullptr);
    // True until the queued shot has actually been taken (end of the frame).
    bool IsScreenshotInProgress() const;

    bool m_InFrame = false;   // between BeginFrame and EndFrame
    int m_FrameLock = 1;
    int GetWidth() const  { return m_Width; }
    int GetHeight() const { return m_Height; }
    int GetCopyToFrontWidth() const  { return m_Width; }
    int GetCopyToFrontHeight() const { return m_Height; }
    float GetFloatWidth() const  { return (float)m_Width; }
    float GetFloatHeight() const { return (float)m_Height; }
    u32 GetZBP() const { return 0; }   // PS2 GS depth-buffer page: no PC equivalent
    u32 GetFBP() const { return 0; }   // PS2 GS frame-buffer page: no PC equivalent

private:
    const void *m_VertexShader = nullptr;
    const void *m_PixelShader = nullptr;
    bool m_Fullscreen = false;        // SetRes(fs) / SetWindow
    bool m_CentreWindow = false;      // SetWindow: centre the desktop window when it opens
    void SizeViewports();
    void ApplyWindowMode();           // style + placement of the open window per the flags above

    int  m_Width;
    int  m_Height;
    bool m_DeviceOpen;
    int  m_FrameNumber;
    u32  m_EventFlags;
    float m_Blit2DTexScaleU = 1.0f;
    float m_Blit2DTexScaleV = 1.0f;
};

class gfxPipeline {
public:
    enum {
        clearColor = 1,
        clearZ = 2,
        clearStencil = 4
    };
    static int GetWidth();
    static int GetHeight();
    static class gfxViewport* CreateViewport();
    static class gfxViewport* GetViewport();     // PIPE.GetViewport()
    static class gfxViewport* OrthoVP;
    static void SetTitle(const char *title);
    // Xbox push-buffer sizing (the D3D11 context queues commands itself);
    // recorded for GetPushBufferSize readers.
    static void SetPushBufferSize(int sz1, int sz2) { sm_PushBufferSize = sz1; sm_PushBufferRunout = sz2; }
    static int GetPushBufferSize() { return sm_PushBufferSize; }
    static int sm_PushBufferSize;
    static int sm_PushBufferRunout;
    static void RequestScreenShot(const char *filename = nullptr);
};

#define PIPE (*pipeManager::sm_Instance)
enum { gfxRenderTargetDoNotResetViewport = 1 };
enum { evtClosed = 1, evtDontBlock = 2, evtLostFocus = 4 };   // evtDontBlock: keep running without focus (mc3 -nofocusblock)

// While suspended, gfxEndFrame does not advance the -shot/-quitafter timers
// (used by the loading-screen pump so captures stay gameplay frames); with
// -shotload <path>, the first suspended frame is captured instead.
extern "C" void gfxSetShotSuspended(bool suspended);

// AGE 2.72 frame boundaries as the game's main loop uses them (mcGame::Execute
// in the kGame state calls ageBeginUpdate / ageBeginDraw / Draw / ageEndDraw
// and nothing else): update = pump the window and input, begin draw = open
// the frame unless one is already open (clear=false: the game clears itself
// via PIPE.Clear / the skyhat - honoured by the backend's clear-on-begin for
// now), end draw = present and run the registered end-of-frame callbacks
// (g_datEndFrameList: the audio managers).
void ageBeginUpdate();
// Nothing to close out: input/window pumping happens in ageBeginUpdate and
// the frame is presented by ageEndDraw.
inline void ageEndUpdate() { }
void ageBeginDraw(bool clear = true);
void ageEndDraw();

#endif // GFX_SIMPLE_H
