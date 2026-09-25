////////////////////////////////////////
// simple.cpp
//
// pipeManager (PIPE): the graphics pipeline / render-loop owner.  Centralises
// the window+device lifecycle, the per-frame begin/clear/end cycle, message
// pumping, resolution, and viewport setup that the testers used to hand-roll
// via the age* globals.
////////////////////////////////////////

#include "memory/heap.h"
#include "gfx/simple.h"
#include "bank/bank.h"
#include "data/callback.h"
#include "data/main.h"       // ageBeginGfx/ageEndGfx, ageClearColor
#include "profile/profiler.h"
#include "profile/ekg.h"
#include <windows.h>
#include <math.h>

extern HWND ageHWND;

static WORD sOriginalGammaRamp[3][256];
static bool sHasOriginalGammaRamp = false;

// D3D11 backend entry points (age/src/gfx/rgl.cpp).
extern "C" void gfxBeginFrame(unsigned clearColor);
extern "C" void gfxEndFrame();
extern "C" void gfxClear(unsigned flags, unsigned clearColor);
extern "C" void gfxScreenshotNow();

static const float PI_F = 3.14159265f;

float gfxFrameTime = 0.0f;

pipeManager* pipeManager::sm_Instance = NULL;

// Statically construct the global pipeline manager instance.
static pipeManager s_PipeManager;

static void sInitAppFromHeap(bool setRes);

pipeManager::pipeManager()
    : VP(NULL), OrthoVP(NULL), m_DefaultVP(NULL), m_Width(640), m_Height(480),
      m_DeviceOpen(false), m_FrameNumber(0), m_EventFlags(0) {
    memHeap::sm_InitAppHook = sInitAppFromHeap;
    sm_Instance = this;
    m_DefaultVP = new gfxViewport();
    VP = m_DefaultVP;
    OrthoVP = new gfxViewport();
    gfxPipeline::OrthoVP = OrthoVP;
    SizeViewports();
}

pipeManager::~pipeManager() {
    delete m_DefaultVP;
    m_DefaultVP = NULL;
    VP = NULL;
    delete OrthoVP;
    OrthoVP = NULL;
    gfxPipeline::OrthoVP = NULL;
    if (sHasOriginalGammaRamp && ageHWND) {
        HDC hdc = GetDC(ageHWND);
        if (hdc) {
            SetDeviceGammaRamp(hdc, sOriginalGammaRamp);
            ReleaseDC(ageHWND, hdc);
        }
        sHasOriginalGammaRamp = false;
    }
    if (sm_Instance == this) {
        sm_Instance = NULL;
    }
}

gfxViewport *pipeManager::CreateViewport() {
    gfxViewport *vp = new gfxViewport();
    vp->SetRect((float)(m_Width > 0 ? m_Width : 640), (float)(m_Height > 0 ? m_Height : 480));
    vp->SetPerspective(60.0f * PI_F / 180.0f, 0.1f, 1000.0f);
    return vp;
}

void pipeManager::SizeViewports() {
    if (VP) {
        VP->SetRect((float)m_Width, (float)m_Height);
        VP->SetPerspective(60.0f * PI_F / 180.0f, 0.1f, 1000.0f);
    }
    if (OrthoVP) {
        OrthoVP->SetRect((float)m_Width, (float)m_Height);
        OrthoVP->OrthoScreen();   // absolute pixels; SetWindow on it scissors (mc3 windowed rendering)
    }
}

// ---------------------------------------------------------------------------
// window / device lifecycle
// ---------------------------------------------------------------------------

void pipeManager::SetTitle(const char *title) {
    if (ageHWND) {
        SetWindowTextA(ageHWND, title);
    }
}

void pipeManager::SetWindow() {
    // Desktop window, centred: the OS window itself is created with the
    // device in ageBeginGfx (which owns the WndProc that feeds input), so
    // record the request and apply it once (or if) the window exists.
    m_Fullscreen = false;
    m_CentreWindow = true;
    if (m_DeviceOpen) ApplyWindowMode();
}

void pipeManager::SetRes(int w, int h, int d, int f, bool fs) {
    if (w > 0) m_Width = w;
    if (h > 0) m_Height = h;
    m_Fullscreen = fs;
    if (m_DeviceOpen) {
        SizeViewports();
        ApplyWindowMode();
    }
}

// Window style/placement for the current full-screen / windowed request.
// Full screen is borderless over the window's monitor (the swapchain keeps
// its size and is stretched, as the Alt+Enter toggle in the host does);
// windowed restores the overlapped frame at the requested client size,
// centred on the primary monitor's work area when SetWindow asked for it.
void pipeManager::ApplyWindowMode() {
    HWND hwnd = ageHWND;
    if (!hwnd) return;
    DWORD style = (DWORD)GetWindowLong(hwnd, GWL_STYLE);
    if (m_Fullscreen) {
        MONITORINFO mi = {sizeof(mi)};
        if (!GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &mi)) return;
        SetWindowLong(hwnd, GWL_STYLE, (LONG)(style & ~WS_OVERLAPPEDWINDOW));
        SetWindowPos(hwnd, HWND_TOPMOST, mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    } else {
        SetWindowLong(hwnd, GWL_STYLE, (LONG)(style | WS_OVERLAPPEDWINDOW));
        RECT rc = {0, 0, m_Width, m_Height};
        AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
        int w = rc.right - rc.left, h = rc.bottom - rc.top;
        UINT flags = SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW;
        int x = 0, y = 0;
        if (m_CentreWindow) {
            RECT work = {0, 0, 0, 0};
            SystemParametersInfo(SPI_GETWORKAREA, 0, &work, 0);
            x = work.left + ((work.right - work.left) - w) / 2;
            y = work.top + ((work.bottom - work.top) - h) / 2;
            if (x < work.left) x = work.left;
            if (y < work.top) y = work.top;
        } else {
            flags |= SWP_NOMOVE;
        }
        SetWindowPos(hwnd, HWND_NOTOPMOST, x, y, w, h, flags);
    }
    ShowWindow(hwnd, SW_SHOW);
}

// Pipeline class init creates the window and opens the device right away at
// the resolution SetRes() stored (memHeap::InitClass calls this through
// memHeap::sm_InitAppHook; games may also call PIPE.InitClass() directly).
void pipeManager::InitClass() {
    Begin();
}

static void sInitAppFromHeap(bool setRes) {
    if (!pipeManager::sm_Instance) return;
    if (setRes) PIPE.SetRes();      // default resolution when the game set none
    PIPE.InitClass();
}

void pipeManager::Begin() {
    if (!m_DeviceOpen) {
        // Creates the window + opens the D3D device, then calls back into
        // OnDeviceOpened() to record dimensions and size the viewports.
        ageBeginGfx(m_Width, m_Height);
    }
}

void pipeManager::End() {
    if (m_DeviceOpen) {
        ageEndGfx();   // closes the device + destroys the window -> OnDeviceClosed()
    }
}

void pipeManager::OnDeviceOpened(int w, int h) {
    if (w > 0) m_Width = w;
    if (h > 0) m_Height = h;
    m_DeviceOpen = true;
    SizeViewports();
    // -fullscreen / -windowed override whatever the game asked for.
    if (args::sm_Instance) {
        if (ARGS.Get("fullscreen")) m_Fullscreen = true;
        else if (ARGS.Get("windowed")) m_Fullscreen = false;
    }
    if (m_Fullscreen || m_CentreWindow) ApplyWindowMode();
}

void pipeManager::OnDeviceClosed() {
    m_DeviceOpen = false;
    if (sHasOriginalGammaRamp && ageHWND) {
        HDC hdc = GetDC(ageHWND);
        if (hdc) {
            SetDeviceGammaRamp(hdc, sOriginalGammaRamp);
            ReleaseDC(ageHWND, hdc);
        }
        sHasOriginalGammaRamp = false;
    }
}

void pipeManager::SetGamma(float exp1, float exp2, int /*flags*/) {
    if (!ageHWND) return;
    HDC hdc = GetDC(ageHWND);
    if (!hdc) return;

    if (!sHasOriginalGammaRamp) {
        if (GetDeviceGammaRamp(hdc, sOriginalGammaRamp)) {
            sHasOriginalGammaRamp = true;
        }
    }

    WORD ramp[3][256];
    for (int i = 0; i < 256; ++i) {
        float v = (float)i / 255.0f;
        float g = powf(v, exp1) * exp2;
        if (g < 0.0f) g = 0.0f;
        if (g > 1.0f) g = 1.0f;
        WORD val = (WORD)(g * 65535.0f + 0.5f);
        ramp[0][i] = val;
        ramp[1][i] = val;
        ramp[2][i] = val;
    }
    SetDeviceGammaRamp(hdc, ramp);
    ReleaseDC(ageHWND, hdc);
}

// ---------------------------------------------------------------------------
// per-frame
// ---------------------------------------------------------------------------

void pipeManager::Manage() {
    if (ageExit()) {
        m_EventFlags |= evtClosed;
    }
}

void pipeManager::BeginFrame() {
    pfProfiler::BeginFrame();
    gfxBeginFrame(ageClearColor);   // the backend clears colour + depth on begin
    ++m_FrameNumber;
    m_InFrame = true;
}

void pipeManager::Clear(u32 flags, u32 color) {
    gfxClear(flags, color);
}

void pipeManager::EndFrame() {
    pfProfiler::EndFrame();
    pfEKGMgr::Draw();
    m_InFrame = false;
    gfxEndFrame();   // presents (vsync-paced); per-frame input reset lives there
}

void pipeManager::FlushFrame() {
    // PS2 code calls this mid-frame to display what it has drawn so far
    // (uiPage2D backgrounds, loading layers).  On PC the frame is composed
    // into one backbuffer and presented once at EndFrame; presenting here
    // would ship a half-drawn frame and alternate it with the real one
    // (the frontend's black flicker).  Only flush when no frame is open.
    if (m_InFrame)
        return;
    gfxEndFrame();
    gfxBeginFrame(ageClearColor);
}

// ---------------------------------------------------------------------------
// widgets
// ---------------------------------------------------------------------------

#if __BANK
void pipeManager::AddWidgets(class bkBank* bank) {
    if (bank) {
        bank->AddButton("Screenshot", datCallback(CFA(gfxScreenshotNow)),
                        "Save the current frame to screenshot_NNN.png");
        pfProfiler::AddWidgets(*bank);
    }
}
#endif // __BANK

gfxViewport* gfxPipeline::OrthoVP = NULL;
int gfxPipeline::sm_PushBufferSize = 0;
int gfxPipeline::sm_PushBufferRunout = 0;
void gfxPipeline::SetTitle(const char *title) { PIPE.SetTitle(title); }

int gfxPipeline::GetWidth() { return PIPE.GetWidth(); }
int gfxPipeline::GetHeight() { return PIPE.GetHeight(); }
gfxViewport* gfxPipeline::CreateViewport() { return PIPE.CreateViewport(); }

gfxTexture *pipeManager::GetRenderFrontBuffer() {
    gfxRefreshBackBufferCopy();
    return gfxGetBackBufferCopy();
}

// The full-screen-effects render buffer is the backbuffer copy (rgl.cpp).
void gfxReleaseBackBufferCopy();
bool gfxHasBackBufferCopy();
void pipeManager::CreateRenderBuffer() { gfxGetBackBufferCopy(); }
void pipeManager::DeleteRenderBuffer() { gfxReleaseBackBufferCopy(); }
bool pipeManager::HasRenderBuffer() const { return gfxHasBackBufferCopy(); }

extern "C" void gfxSaveScreenshot(const char *path);
extern "C" void gfxQueueScreenshot(const char *path);
extern "C" bool gfxScreenshotQueued();

void pipeManager::RequestScreenShot(const char *filename) {
    // Queue it rather than capture here: the callers are widget callbacks and
    // per-frame game code that run before the frame they want has been drawn.
    gfxQueueScreenshot(filename ? filename : "screenshot.png");
}

// True while a queued shot has not been taken yet.  The movie capture and the
// car-shot loop use this to pace themselves one request at a time.
bool pipeManager::IsScreenshotInProgress() const { return gfxScreenshotQueued(); }

#include "gfx/image.h"
#include "gfx/texture.h"
#include <stdlib.h>

extern "C" bool gfxReadbackBackBuffer(unsigned char *rgba, int x, int y, int w, int h);

gfxViewport *gfxPipeline::GetViewport() { return PIPE.GetViewport(); }

// The finished frame as a bindable texture (fullscreen post effects).
gfxTexture *pipeManager::GetRenderBackBuffer() {
    gfxTexture *copy = gfxGetBackBufferCopy();
    if (copy) gfxRefreshBackBufferCopy();
    return copy;
}

gfxImage *pipeManager::CreateReadbackImage() {
    gfxImage *img = new gfxImage;
    img->Width = GetWidth() > 0 ? GetWidth() : 1;
    img->Height = GetHeight() > 0 ? GetHeight() : 1;
    img->m_RGBA = calloc((size_t)img->Width * img->Height, 4);
    return img;
}

void pipeManager::Readback(gfxImage *img, int x, int y, int w, int h) {
    if (!img || !img->m_RGBA) return;
    if (w <= 0) w = img->Width;
    if (h <= 0) h = img->Height;
    if (w > img->Width) w = img->Width;
    if (h > img->Height) h = img->Height;
    gfxReadbackBackBuffer((unsigned char *)img->m_RGBA, x, y, w, h);
}

////////////////////////////////////////
// frame boundaries used by the game loop

#include "data/callback.h"

void ageBeginUpdate() {
    PIPE.Manage();
}

void ageBeginDraw(bool clear) {
    (void)clear;
    if (!PIPE.IsInFrame())
        PIPE.BeginFrame();
}

void ageEndDraw() {
    if (PIPE.IsInFrame())
        PIPE.EndFrame();
    g_datEndFrameList.Call();
}

////////////////////////////////////////
// render targets / screenshots

extern "C" void gfxScreenshotNow();
void gfxBeginRenderToTexture(gfxTexture *tex);
void gfxBeginRenderToTexture(gfxTexture *tex, int flags, unsigned clearColor);
void gfxEndRenderToTexture();

static gfxTexture *s_CurrentTarget = 0;
static int s_CurrentTargetFlags = 0;

static void sSetRenderTarget(gfxTexture *tex, int flags) {
    if (tex == s_CurrentTarget && flags == s_CurrentTargetFlags) return;
    if (s_CurrentTarget) gfxEndRenderToTexture();
    s_CurrentTarget = tex;
    s_CurrentTargetFlags = flags;
    if (tex) gfxBeginRenderToTexture(tex, flags, 0x00000000u);
}

void pipeManager::SetRenderTarget(gfxTexture *tex, int /*arg2*/, int /*arg3*/) {
    sSetRenderTarget(tex, 0);
}

void pipeManager::SetRenderTarget(gfxTexture *tex, gfxTexture *zbuffer) {
    // A zbuffer means a 3D scene, which owns its own clear (PIPE.Clear) and may
    // bind the same target repeatedly in one frame to build the image up.
    sSetRenderTarget(tex, zbuffer ? (gfxRTTDepth | gfxRTTNoClear) : 0);
}

void gfxPipeline::RequestScreenShot(const char *filename) {
    PIPE.RequestScreenShot(filename);
}
