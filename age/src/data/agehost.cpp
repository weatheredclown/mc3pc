////////////////////////////////////////
// agehost.cpp
//
// The Win32 host for games that bring their own main() (MC3's mc.cpp via
// mcproduct/mcmain.h): the render window and its message pump, keyboard /
// mouse delivery into the io* layer, the pipeline's ageBeginGfx/ageEndGfx
// callbacks, the per-frame ageBeginFrame/ageEndFrame/ageExit boilerplate
// and ExceptMain (crash-filtered call of the game's Main()).  data/main.cpp
// is the rb-era host that also supplies main(); the two are not linked
// together.
////////////////////////////////////////

#include "vector/random.h"
#include "data/main.h"
#include "data/args.h"
#include "data/assetcfg.h"
#include "gfx/misc.h"
#include "input/keyboard.h"
#include "input/mouse.h"
#include "input/pad.h"
#include <windows.h>
#include <stdio.h>

// Window icon resource id (the game sets it before the pipeline opens the window).
int gfxIcon = 0;
// Debug hook run by the fatal-error path (Quitf) before exiting; games may install one.
void (*ageExternalDebugHandler)() = nullptr;

// D3D11 renderer backend (age/src/gfx/rgl.cpp).
extern "C" void gfxOpenDevice(void *hwnd, int w, int h);
extern "C" void gfxBeginFrame(unsigned clearColor);
extern "C" void gfxEndFrame();
extern "C" void gfxCloseDevice();
// The -shot auto-screenshot lives in gfxEndFrame (universal per-frame present);
// it requests quit through this flag once it has captured.
extern "C" bool gfxWantsExit();

u32 ageClearColor = 0;
HWND ageHWND = NULL;
HWND hwndMain = NULL;   // exported via gfx/winpriv.h (editor menu bar, dialogs)
bool isMaximized = false;
static bool s_ExitRequested = false;

// Clean programmatic quit (the -runall sweep ends this way).
extern "C" void ageRequestExit() { s_ExitRequested = true; }

static int MapVirtualKeyToAgeKey(WPARAM wParam) {
  if (wParam >= '0' && wParam <= '9')
    return (int)wParam;
  if (wParam >= 'A' && wParam <= 'Z')
    return (int)wParam;
  switch (wParam) {
  case VK_ESCAPE:
    return KEY_ESCAPE;
  case VK_SPACE:
    return KEY_SPACE;
  case VK_RETURN:
    return KEY_ENTER;
  case VK_TAB:
    return KEY_TAB;
  case VK_PRIOR:
    return KEY_PAGEUP;
  case VK_NEXT:
    return KEY_PAGEDOWN;
  case VK_HOME:
    return KEY_HOME;
  case VK_END:
    return KEY_END;
  case VK_INSERT:
    return KEY_INSERT;
  case VK_DELETE:
    return KEY_DELETE;
  case VK_F1:
    return KEY_F1;
  case VK_F2:
    return KEY_F2;
  case VK_F3:
    return KEY_F3;
  case VK_F4:
    return KEY_F4;
  case VK_F5:
    return KEY_F5;
  case VK_F6:
    return KEY_F6;
  case VK_F7:
    return KEY_F7;
  case VK_F8:
    return KEY_F8;
  case VK_F9:
    return KEY_F9;
  case VK_F10:
    return KEY_F10;
  case VK_F11:
    return KEY_F11;
  case VK_F12:
    return KEY_F12;
  case VK_LEFT:
    return KEY_LEFT;
  case VK_RIGHT:
    return KEY_RIGHT;
  case VK_UP:
    return KEY_UP;
  case VK_DOWN:
    return KEY_DOWN;
  case VK_SHIFT:
    return KEY_SHIFT;
  case VK_CONTROL:
    return KEY_RCTRL;
  case VK_NUMPAD0:
    return KEY_NUMPAD0;
  case VK_NUMPAD1:
    return KEY_NUMPAD1;
  case VK_NUMPAD2:
    return KEY_NUMPAD2;
  case VK_NUMPAD3:
    return KEY_NUMPAD3;
  case VK_NUMPAD4:
    return KEY_NUMPAD4;
  case VK_NUMPAD5:
    return KEY_NUMPAD5;
  case VK_NUMPAD6:
    return KEY_NUMPAD6;
  case VK_NUMPAD7:
    return KEY_NUMPAD7;
  case VK_NUMPAD8:
    return KEY_NUMPAD8;
  case VK_NUMPAD9:
    return KEY_NUMPAD9;
  case VK_SUBTRACT:
    return KEY_SUBTRACT;
  case VK_ADD:
    return KEY_ADD;
  case VK_MENU:
    return KEY_ALT;
  case VK_OEM_MINUS:
    return KEY_MINUS;
  case VK_BACK:
    return KEY_BACK;
  }
  return -1;
}

#include "bank/bkmgr.h"

static void ToggleFullscreen(HWND hwnd) {
  static WINDOWPLACEMENT g_wpPrev = {sizeof(g_wpPrev)};
  DWORD dwStyle = GetWindowLong(hwnd, GWL_STYLE);
  if (dwStyle & WS_OVERLAPPEDWINDOW) {
    MONITORINFO mi = {sizeof(mi)};
    if (GetWindowPlacement(hwnd, &g_wpPrev) &&
        GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY),
                       &mi)) {
#if __BANK
      if (bkManager::IsEnabled()) {
        BANKMGR.SetFullscreen(true);
      }
#endif
      SetWindowLong(hwnd, GWL_STYLE, dwStyle & ~WS_OVERLAPPEDWINDOW);
      SetWindowPos(hwnd, HWND_TOPMOST, mi.rcMonitor.left, mi.rcMonitor.top,
                   mi.rcMonitor.right - mi.rcMonitor.left,
                   mi.rcMonitor.bottom - mi.rcMonitor.top,
                   SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
      ShowWindow(hwnd, SW_SHOW);
      SetForegroundWindow(hwnd);
      SetFocus(hwnd);
    }
  } else {
    SetWindowLong(hwnd, GWL_STYLE, dwStyle | WS_OVERLAPPEDWINDOW);
    SetWindowPlacement(hwnd, &g_wpPrev);
    SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);
#if __BANK
    if (bkManager::IsEnabled()) {
      BANKMGR.SetFullscreen(false);
    }
#endif
  }
}

static bool s_FullscreenKeyHeld = false;

static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam,
                                LPARAM lParam) {
  switch (message) {
  case WM_KEYDOWN:
  case WM_SYSKEYDOWN: {
    if (wParam == VK_RETURN && ((GetKeyState(VK_MENU) & 0x8000) != 0 || (lParam & (1 << 29)) != 0)) {
      if (!(lParam & (1 << 30)) && !s_FullscreenKeyHeld) {
        s_FullscreenKeyHeld = true;
        ToggleFullscreen(hWnd);
      }
      return 0;
    }
    int ageKey = MapVirtualKeyToAgeKey(wParam);
    if (ageKey != -1) {
      ioKeyboard::SetKeyDown(ageKey, true);
    }
    break;
  }
  case WM_SYSCOMMAND: {
    if ((wParam & 0xFFF0) == SC_KEYMENU) {
      return 0;
    }
    break;
  }
  case WM_COMMAND: {
#if __BANK
    // Editor menu-bar picks (bank/menubar.h routes them to the registered
    // callback).
    extern bool bkMenuBarDispatch(int id);
    if (bkMenuBarDispatch((int)(wParam & 0xFFFF)))
      return 0;
#endif
    break;
  }
  case WM_KEYUP:
  case WM_SYSKEYUP: {
    if (wParam == VK_RETURN || wParam == VK_MENU) {
      s_FullscreenKeyHeld = false;
    }
    int ageKey = MapVirtualKeyToAgeKey(wParam);
    if (ageKey != -1) {
      ioKeyboard::SetKeyDown(ageKey, false);
    }
    break;
  }
  case WM_MOUSEMOVE: {
    RECT rc;
    if (GetClientRect(hWnd, &rc))
      ioMouse::SetScreenSize(rc.right - rc.left, rc.bottom - rc.top);
    ioMouse::SetPosition((int)(short)LOWORD(lParam),
                         (int)(short)HIWORD(lParam));
    break;
  }
  case WM_LBUTTONDOWN:
  case WM_LBUTTONUP: {
    ioMouse::SetButtonState(ioMouse::mouseLeft, message == WM_LBUTTONDOWN);
    break;
  }
  case WM_RBUTTONDOWN:
  case WM_RBUTTONUP: {
    ioMouse::SetButtonState(ioMouse::mouseRight, message == WM_RBUTTONDOWN);
    break;
  }
  case WM_MBUTTONDOWN:
  case WM_MBUTTONUP: {
    ioMouse::SetButtonState(ioMouse::mouseMiddle, message == WM_MBUTTONDOWN);
    break;
  }
  case WM_MOUSEWHEEL: {
    ioMouse::AddWheelDelta((int)(short)HIWORD(wParam) / WHEEL_DELTA);
    break;
  }
  case WM_DESTROY:
    s_ExitRequested = true;
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProc(hWnd, message, wParam, lParam);
}


void (*ageInitAnimHook)() = nullptr;
void (*ageShutdownAnimHook)() = nullptr;

void ageInit(const char *path, bool arg2, bool arg3) {
  CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
  const char *overridePath = NULL;
  if (ARGS.Get("path", 0, &overridePath) && overridePath) {
    ASSET.SetPath(overridePath);
  } else if (ARGS.Get("dat", 0, &overridePath) && overridePath) {
    ASSET.SetPath(overridePath);
  } else if (path) {
    ASSET.SetPath(path);
  }
  if (ageInitAnimHook) {
    ageInitAnimHook();
  }
}

#include "gfx/simple.h"

void ageBeginGfx(int width, int height, int arg3, int arg4, bool windowed) {
  if (!ARGS.Get("w", width, width)) {
    ARGS.Get("width", width, width);
  }
  if (!ARGS.Get("h", height, height)) {
    ARGS.Get("height", height, height);
  }

  WNDCLASSEX wc = {sizeof(WNDCLASSEX),    CS_CLASSDC, WndProc, 0L,   0L,
                   GetModuleHandle(NULL), NULL,       NULL,    NULL, NULL,
                   "age_window",          NULL};
  RegisterClassEx(&wc);
  ageHWND = CreateWindow("age_window", "AGE Window", WS_OVERLAPPEDWINDOW, 100,
                         100, width, height, NULL, NULL, wc.hInstance, NULL);
  hwndMain = ageHWND;
  ShowWindow(ageHWND, SW_SHOWDEFAULT);
  UpdateWindow(ageHWND);
  gfxOpenDevice(ageHWND, width, height);
  PIPE.OnDeviceOpened(width, height); // record dims + size viewports
}

void ageEndGfx() {
  gfxCloseDevice();
  PIPE.OnDeviceClosed();
  if (ageHWND) {
    DestroyWindow(ageHWND);
    ageHWND = NULL;
  }
  hwndMain = NULL;
  UnregisterClass("age_window", GetModuleHandle(NULL));
  if (ageShutdownAnimHook) {
    ageShutdownAnimHook();
  }
  CoUninitialize();
}

// The per-frame begin/end now live in pipeManager; these globals delegate to it
// so both the age* boilerplate and the rb shell share one implementation.
void ageBeginFrame() { PIPE.BeginFrame(); }

void ageEndFrame() { PIPE.EndFrame(); }

bool ageExit() {
  MSG msg;
  while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
    if (msg.message == WM_QUIT || msg.message == WM_CLOSE || msg.message == WM_DESTROY) {
      Displayf("[ageExit] msg: message=%d, s_ExitRequested=%d, gfxWantsExit=%d", 
               msg.message, s_ExitRequested, gfxWantsExit());
    }
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
  ioPad::PollHardware();
  bool exit = s_ExitRequested || gfxWantsExit();
  if (exit) {
    Displayf("[ageExit] returning true! s_ExitRequested=%d, gfxWantsExit=%d", s_ExitRequested, gfxWantsExit());
  }
  return exit;
}


extern int Main();

// Print a symbolized stack trace on unhandled exceptions (same behavior as
// testactorshell's handler); without this rbmain dies silently on a crash.
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
static LONG WINAPI RbMainCrashFilter(EXCEPTION_POINTERS *xp) {
  // dbghelp is not thread-safe and a crash can fire on several threads at
  // once (which interleaves the output and makes SymFromAddr fail — bare
  // addresses). First crashing thread wins; the rest just wait here.
  static volatile LONG sInFilter = 0;
  if (InterlockedCompareExchange(&sInFilter, 1, 0) != 0) {
    Sleep(10000);
    return EXCEPTION_EXECUTE_HANDLER;
  }
  if (xp)  // NULL when called from the SIGABRT handler below
    fprintf(stderr, "Unhandled Exception caught! Code: 0x%08X, Address: 0x%p\n",
            xp->ExceptionRecord->ExceptionCode,
            xp->ExceptionRecord->ExceptionAddress);
  void *stack[64];
  USHORT frames = RtlCaptureStackBackTrace(0, 64, stack, NULL);
  fprintf(stderr, "----- Raw Stack Trace (%d frames) -----\n", frames);
  for (USHORT i = 0; i < frames; i++) {
    fprintf(stderr, "  [%d] 0x%p\n", i, stack[i]);
  }
  fflush(stderr);

  HANDLE process = GetCurrentProcess();
  char searchPath[1024] = {0};
  char exePath[512] = {0};
  GetModuleFileNameA(NULL, exePath, sizeof(exePath));
  char *lastSlash = strrchr(exePath, '\\');
  if (lastSlash) *lastSlash = 0;
  sprintf_s(searchPath, "%s;.;%s\\..;%s\\lib", exePath, exePath, exePath);

  SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
  SymInitialize(process, searchPath, TRUE);
  char symbolBuffer[sizeof(SYMBOL_INFO) + 256];
  SYMBOL_INFO *symbol = (SYMBOL_INFO *)symbolBuffer;
  symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
  symbol->MaxNameLen = 255;
  Displayf("----- Stack Trace -----");
  for (USHORT i = 0; i < frames; i++) {
    DWORD64 address = (DWORD64)stack[i];
    DWORD64 disp64 = 0;
    if (SymFromAddr(process, address, &disp64, symbol)) {
      IMAGEHLP_LINE64 line;
      line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
      DWORD dispLine = 0;
      if (SymGetLineFromAddr64(process, address, &dispLine, &line))
        Displayf("  [%d] %s at %s:%d (0x%p)", i, symbol->Name, line.FileName,
                 line.LineNumber, (void *)address);
      else
        Displayf("  [%d] %s (0x%p)", i, symbol->Name, (void *)address);
    } else {
      Displayf("  [%d] 0x%p", i, (void *)address);
    }
  }
  Displayf("-----------------------");

  // A call through a freed object's vtable faults with RIP outside the image
  // and the walk above stops there.  The caller's return address is still on
  // the faulting thread's stack, so scan it and symbolize anything that points
  // into mc.exe (innermost first; stale slots can show up too).
  if (xp && xp->ContextRecord) {
    uintptr_t base = (uintptr_t)GetModuleHandle(NULL);
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    uintptr_t end = base + nt->OptionalHeader.SizeOfImage;
    uintptr_t rip = (uintptr_t)xp->ContextRecord->Rip;
    if (rip < base || rip >= end) {
      uintptr_t rsp = (uintptr_t)xp->ContextRecord->Rsp;
      Displayf("----- Stack scan from RSP 0x%p (RIP outside image) -----", (void *)rsp);
      int shown = 0;
      for (int slot = 0; slot < 512 && shown < 24; slot++) {
        uintptr_t *p = (uintptr_t *)(rsp + slot * sizeof(uintptr_t));
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(p, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT ||
            (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
          break;
        DWORD64 address = (DWORD64)*p;
        if (address < base || address >= end) continue;
        DWORD64 disp64 = 0;
        if (!SymFromAddr(process, address, &disp64, symbol)) continue;
        IMAGEHLP_LINE64 line;
        line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
        DWORD dispLine = 0;
        if (SymGetLineFromAddr64(process, address, &dispLine, &line))
          Displayf("  [rsp+0x%x] %s at %s:%d", slot * 8, symbol->Name, line.FileName, line.LineNumber);
        else
          Displayf("  [rsp+0x%x] %s+0x%llx", slot * 8, symbol->Name, (unsigned long long)disp64);
        shown++;
      }
      Displayf("-----------------------");
    }
  }
  return EXCEPTION_EXECUTE_HANDLER;
}

// PC port: Catch crashes in VEH first-chance with zero-allocation raw Win32 output
static LONG WINAPI RbHeapCorruptionVEH(EXCEPTION_POINTERS *xp) {
  DWORD code = xp ? (xp->ExceptionRecord ? xp->ExceptionRecord->ExceptionCode : 0) : 0;
  if (code == 0xC0000005 || code == 0xC0000374 || code == 0xC00000FD) {
    char buf[1024];
    DWORD written = 0;
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);

    uintptr_t base = (uintptr_t)GetModuleHandle(NULL);
    uintptr_t rip = xp && xp->ContextRecord ? (uintptr_t)xp->ContextRecord->Rip : 0;
    uintptr_t addr = xp && xp->ExceptionRecord ? (uintptr_t)xp->ExceptionRecord->ExceptionAddress : 0;
    uintptr_t target = (xp && xp->ExceptionRecord && xp->ExceptionRecord->NumberParameters > 1) ? 
                        (uintptr_t)xp->ExceptionRecord->ExceptionInformation[1] : 0;
    int isWrite = (xp && xp->ExceptionRecord && xp->ExceptionRecord->NumberParameters > 0) ? 
                  (int)xp->ExceptionRecord->ExceptionInformation[0] : -1;

    wsprintfA(buf, "\n\n========================================\n"
                   "CRASH VEH: ExceptionCode=0x%08X\n"
                   "  Fault Address: 0x%p (mc.exe + 0x%IX)\n"
                   "  RIP: 0x%p (mc.exe + 0x%IX)\n"
                   "  Access: %s address 0x%p\n"
                   "========================================\n",
              code, (void*)addr, (addr >= base ? addr - base : 0),
              (void*)rip, (rip >= base ? rip - base : 0),
              (isWrite == 1 ? "WRITE to" : (isWrite == 0 ? "READ from" : "ACCESS")),
              (void*)target);

    WriteFile(hErr, buf, (DWORD)strlen(buf), &written, NULL);
    WriteFile(hOut, buf, (DWORD)strlen(buf), &written, NULL);

    void *stack[32];
    USHORT frames = RtlCaptureStackBackTrace(0, 32, stack, NULL);
    wsprintfA(buf, "----- Raw Stack Frames (%u) -----\n", (UINT)frames);
    WriteFile(hErr, buf, (DWORD)strlen(buf), &written, NULL);
    WriteFile(hOut, buf, (DWORD)strlen(buf), &written, NULL);

    for (USHORT i = 0; i < frames; i++) {
      uintptr_t faddr = (uintptr_t)stack[i];
      wsprintfA(buf, "  [%02u] 0x%p (mc.exe + 0x%IX)\n", (UINT)i, (void*)faddr, (faddr >= base ? faddr - base : 0));
      WriteFile(hErr, buf, (DWORD)strlen(buf), &written, NULL);
      WriteFile(hOut, buf, (DWORD)strlen(buf), &written, NULL);
    }
    wsprintfA(buf, "---------------------------------\n\n");
    WriteFile(hErr, buf, (DWORD)strlen(buf), &written, NULL);
    WriteFile(hOut, buf, (DWORD)strlen(buf), &written, NULL);
    FlushFileBuffers(hErr);
    FlushFileBuffers(hOut);

    RbMainCrashFilter(xp);
    ExitProcess(code);
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

// Debug aid: validate the CRT + process heaps and shout if they are already
// corrupt — call between suspect steps to localize heap corruption at the
// first *detection* point instead of wherever ntdll finally trips.
#include <malloc.h>
extern "C" void ageHeapCheck(const char *tag) {
  static const bool sHeapLog = (getenv("MC_HEAPLOG") != NULL);
  if (sHeapLog) {
    Displayf("[heapcheck] start: %s", tag ? tag : "?");
    fflush(stdout);
  }
  int crt = _heapchk();
  BOOL win = HeapValidate(GetProcessHeap(), 0, NULL);
  if (crt != _HEAPOK || !win) {
    Displayf("[heapcheck] %s: CORRUPT (crt=%d win=%d)", tag ? tag : "?", crt,
             (int)win);
    RbMainCrashFilter(NULL); // print where we noticed it
  }
}

// abort() (failed Assert, Quitf, uncaught C++ exception -> terminate) exits
// with code 3 and BYPASSES the SEH filter above — hook SIGABRT so those print
// a stack too instead of dying silently.
#include <signal.h>
static void RbMainAbortHandler(int) {
  Displayf("SIGABRT (abort/assert/terminate) caught!");
  RbMainCrashFilter(NULL);
  _exit(3);
}

extern "C" void ageDumpCallstack() {
  RbMainCrashFilter(NULL);
}



// Run the game's Main() under the crash filters so a fault prints a stack
// instead of dying silently (mcmain.h's main() calls this).
int ExceptMain() {
  setvbuf(stdout, NULL, _IONBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);
  printf(">>> ExceptMain() initialized crash handlers <<<\n");
  fflush(stdout);
  SetUnhandledExceptionFilter(RbMainCrashFilter);
  AddVectoredExceptionHandler(1, RbHeapCorruptionVEH);
  signal(SIGABRT, RbMainAbortHandler);
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
  // -randseed <n>: pin the global random streams.  Here rather than in ageInit
  // because the game's host (mcproduct/mcmain.h) calls ExceptMain directly and
  // never calls ageInit; the command line is already parsed by this point.
  randInitFromArgs();
  return Main();
}
