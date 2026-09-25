#include "input/pad.h"
#include "data/args.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <math.h>
#include <string.h>
#endif

ioPad ioPad::sm_Pads[4];
bool ioPad::sm_UseKeymap = false;
bool ioPad::sm_SupportMultitap = false;

ioPad::ioPad()
    : m_Buttons(0), m_KeyboardButtons(0), m_ButtonsPressed(0), m_ButtonsReleased(0),
      m_LeftX(0.0f), m_LeftY(0.0f), m_RightX(0.0f), m_RightY(0.0f),
      m_Connected(false) {}

ioPad::~ioPad() {}

unsigned int ioPad::GetButtons() const {
    return m_Buttons;
}

unsigned int ioPad::GetPressedButtons() const {
    return m_ButtonsPressed;
}

unsigned int ioPad::GetReleasedButtons() const {
    return m_ButtonsReleased;
}

float ioPad::GetNormRightX() const { return m_RightX; }
float ioPad::GetNormRightY() const { return m_RightY; }
float ioPad::GetNormLeftX() const { return m_LeftX; }
float ioPad::GetNormLeftY() const { return m_LeftY; }

void ioPad::SetButtons(unsigned int buttons) {
    unsigned int diff = buttons ^ m_Buttons;
    m_ButtonsPressed |= (diff & buttons);
    m_ButtonsReleased |= (diff & ~buttons);
    m_Buttons = buttons;
}

void ioPad::SetNormLeft(float lx, float ly) {
    m_LeftX = lx;
    m_LeftY = ly;
}

void ioPad::SetNormRight(float rx, float ry) {
    m_RightX = rx;
    m_RightY = ry;
}

void ioPad::ClearEdges() {
    m_ButtonsPressed = 0;
    m_ButtonsReleased = 0;
}

ioPad & ioPad::GetPad(int index) {
    if (index < 0 || index >= 4) return sm_Pads[0];
    return sm_Pads[index];
}

void ioPad::SetUseKeymap(bool use) {
    sm_UseKeymap = use;
}

bool ioPad::IsMultitapConnected(int port) {
    return false;
}

void ioPad::SetSupportMultitap(bool support) {
    sm_SupportMultitap = support;
}

bool ioPad::IsConnected(int index) {
    if (index < 0 || index >= 4) return false;
    return sm_Pads[index].m_Connected;
}

#if defined(_WIN32)

// --- XInput, resolved dynamically so we need no import lib and degrade cleanly
// on machines without the runtime. XInputGetState lives in xinput1_4.dll (Win8+)
// with older fallbacks; we take whichever loads first. --------------------------

namespace {

struct XInputGamepad {
    unsigned short wButtons;
    unsigned char  bLeftTrigger;
    unsigned char  bRightTrigger;
    short          sThumbLX, sThumbLY;
    short          sThumbRX, sThumbRY;
};
struct XInputState {
    unsigned long dwPacketNumber;
    XInputGamepad Gamepad;
};

typedef unsigned long (__stdcall *XInputGetStateFn)(unsigned long, XInputState *);

struct XInputVibration {
    unsigned short wLeftMotorSpeed;    // large / low frequency
    unsigned short wRightMotorSpeed;   // small / high frequency
};
typedef unsigned long (__stdcall *XInputSetStateFn)(unsigned long, XInputVibration *);

// XInput button bits (from XInput.h) -- kept local so we don't depend on the SDK header.
enum {
    XI_DPAD_UP = 0x0001, XI_DPAD_DOWN = 0x0002, XI_DPAD_LEFT = 0x0004, XI_DPAD_RIGHT = 0x0008,
    XI_START = 0x0010, XI_BACK = 0x0020, XI_LTHUMB = 0x0040, XI_RTHUMB = 0x0080,
    XI_LSHOULDER = 0x0100, XI_RSHOULDER = 0x0200,
    XI_A = 0x1000, XI_B = 0x2000, XI_X = 0x4000, XI_Y = 0x8000
};
const int   XI_TRIGGER_THRESHOLD = 30;   // XINPUT_GAMEPAD_TRIGGER_THRESHOLD
const short XI_LEFT_DEADZONE  = 7849;     // XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE
const short XI_RIGHT_DEADZONE = 8689;     // XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE

XInputGetStateFn ResolveXInput() {
    static bool tried = false;
    static XInputGetStateFn fn = 0;
    if (!tried) {
        tried = true;
        const char *dlls[] = { "xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll" };
        for (int i = 0; i < 3 && !fn; ++i) {
            HMODULE h = LoadLibraryA(dlls[i]);
            if (h)
                fn = (XInputGetStateFn)GetProcAddress(h, "XInputGetState");
        }
    }
    return fn;
}

// Same dance for the rumble entry point, from whichever runtime loaded.
XInputSetStateFn ResolveXInputSet() {
    static bool tried = false;
    static XInputSetStateFn fn = 0;
    if (!tried) {
        tried = true;
        const char *dlls[] = { "xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll" };
        for (int i = 0; i < 3 && !fn; ++i) {
            HMODULE h = LoadLibraryA(dlls[i]);
            if (h)
                fn = (XInputSetStateFn)GetProcAddress(h, "XInputSetState");
        }
    }
    return fn;
}

// Normalize a thumbstick axis to [-1,1] with a radial-style per-axis deadzone,
// rescaled so motion begins at 0 just past the deadzone. NOTE: XInput's +Y is
// up, but AGE's pad convention (PS2-era, what bhPad's heading math expects) is
// +Y = down/back -- the left-stick Y is negated at the read site below.
// (Verified empirically: feeding XInput's +Y=up convention through bhPad makes
// "stick down" run away from the camera.)
float NormAxis(short v, short deadzone) {
    float f = (v < 0) ? (v / 32768.0f) : (v / 32767.0f);
    float dz = deadzone / 32767.0f;
    float mag = (f < 0.0f) ? -f : f;
    if (mag <= dz) return 0.0f;
    float sign = (f < 0.0f) ? -1.0f : 1.0f;
    return sign * ((mag - dz) / (1.0f - dz));
}

} // namespace

#if __EDITOR
// The editor uses the arrow keys itself (graph/vertex nudging, camera): don't
// alias them onto the d-pad there.
bool ioPad::sm_KeyboardDpadEnabled = false;
#else
bool ioPad::sm_KeyboardDpadEnabled = true;
#endif

void ioPad::SetKeyboardDpadEnabled(bool enable) { sm_KeyboardDpadEnabled = enable; }

bool ioPad::sm_KeyboardDpadFace = false;   // menus first (frontend boots active)
void ioPad::SetKeyboardDpadCluster(bool faceButtons) { sm_KeyboardDpadFace = faceButtons; }

// Push this pad's held motor speeds to the device (nothing on a pad that is
// not connected, or when the actuators are switched off).
void ioPad::PushRumble() {
    // -norumble silences the motors everywhere.  mc3 has its own -nofeedback
    // (localoptions.cpp sets FF_OFF per player), but that is a game-side
    // option; this is the engine-level kill switch, so every path that reaches
    // an actuator goes quiet regardless of who asked.
    static int s_noRumble = -1;
    if (s_noRumble < 0) s_noRumble = ARGS.Get("norumble") ? 1 : 0;
    if (s_noRumble) return;

    XInputSetStateFn setState = ResolveXInputSet();
    if (!setState) return;
    const int index = (int)(this - sm_Pads);
    if (index < 0 || index >= 4 || !m_Connected) return;
    XInputVibration v;
    v.wLeftMotorSpeed  = m_ActuatorsActive ? m_Motor[1] : 0;   // large
    v.wRightMotorSpeed = m_ActuatorsActive ? m_Motor[0] : 0;   // small
    setState((unsigned long)index, &v);
}

void ioPad::SetActuatorValue(int actuator, float value) {
    // Console semantics (0x22efe8): clamp to [0,1], then actuator 0 (kSmall) is
    // the DualShock2's on/off motor - (int)value, so only 1.0 switches it on -
    // and actuator 1 (kLarge) scales to the pad's 0..255.  XInput's right motor
    // is the small/high-frequency one, its left motor the large one.
    if (value < 0.0f) value = 0.0f;
    if (value > 1.0f) value = 1.0f;
    if (actuator == kSmall) {
        m_Motor[0] = ((int)value != 0) ? 0xffff : 0;
    } else if (actuator == kLarge) {
        const int pad255 = (int)(value * 255.0f);
        m_Motor[1] = (unsigned short)((pad255 * 0xffff) / 255);
    } else {
        return;
    }
    PushRumble();
}

void ioPad::SetActuatorsActivation(bool active) {
    m_ActuatorsActive = active;
    PushRumble();               // switching off stops the motors immediately
}

void ioPad::PollHardware() {
    XInputGetStateFn getState = ResolveXInput();

    for (int i = 0; i < 4; ++i) {
        ioPad &pad = sm_Pads[i];
        // Recompute this frame's pressed/released edges from scratch.
        pad.ClearEdges();

        unsigned int buttons = 0;
        float lx = 0.0f, ly = 0.0f, rx = 0.0f, ry = 0.0f;
        bool connected = false;

        if (getState) {
            XInputState st;
            memset(&st, 0, sizeof(st));
            if (getState((unsigned long)i, &st) == 0 /*ERROR_SUCCESS*/) {
                connected = true;
                unsigned short wb = st.Gamepad.wButtons;
                // D-pad -> left directional cluster.
                if (wb & XI_DPAD_UP)    buttons |= Lup;
                if (wb & XI_DPAD_DOWN)  buttons |= Ldown;
                if (wb & XI_DPAD_LEFT)  buttons |= Lleft;
                if (wb & XI_DPAD_RIGHT) buttons |= Lright;
                // Face buttons -> right cluster (Y/B/A/X = up/right/down/left).
                if (wb & XI_Y) buttons |= Rup;
                if (wb & XI_B) buttons |= Rright;
                if (wb & XI_A) buttons |= Rdown;
                if (wb & XI_X) buttons |= Rleft;
                // Shoulders, stick clicks, start/select.
                if (wb & XI_LSHOULDER) buttons |= L1;
                if (wb & XI_RSHOULDER) buttons |= R1;
                if (wb & XI_LTHUMB)    buttons |= L3;
                if (wb & XI_RTHUMB)    buttons |= R3;
                if (wb & XI_START)     buttons |= Start;
                if (wb & XI_BACK)      buttons |= Select;
                // Analog triggers -> L2/R2.
                if (st.Gamepad.bLeftTrigger  > XI_TRIGGER_THRESHOLD) buttons |= L2;
                if (st.Gamepad.bRightTrigger > XI_TRIGGER_THRESHOLD) buttons |= R2;

                lx = NormAxis(st.Gamepad.sThumbLX, XI_LEFT_DEADZONE);
                ly = -NormAxis(st.Gamepad.sThumbLY, XI_LEFT_DEADZONE);  // AGE/PS2 convention: +Y = down
                rx = -NormAxis(st.Gamepad.sThumbRX, XI_RIGHT_DEADZONE); // Inverted so stick right pans camera right
                ry = NormAxis(st.Gamepad.sThumbRY, XI_RIGHT_DEADZONE);
            }
        }

        // Wire up WASD keyboard keys to left analog stick for primary player (pad 0)
        if (i == 0) {
            float kbdLx = 0.0f;
            float kbdLy = 0.0f;
            // AGE/PS2 stick convention: +Y = down/back, so W (forward) is -1.
            // Up/Down join W/S on the stick: they are the throttle keys, and
            // this route does not depend on the window's keyboard focus the way
            // ioKeyboard does.  They still alias to the d-pad below for menus -
            // no menu double-steps on that, because a mapper takes the largest
            // of the sources feeding one value rather than summing them.
            if ((GetAsyncKeyState('W') & 0x8000) || (GetAsyncKeyState(VK_UP)   & 0x8000)) kbdLy -= 1.0f;
            if ((GetAsyncKeyState('S') & 0x8000) || (GetAsyncKeyState(VK_DOWN) & 0x8000)) kbdLy += 1.0f;
            if (GetAsyncKeyState('A') & 0x8000) kbdLx -= 1.0f;
            if (GetAsyncKeyState('D') & 0x8000) kbdLx += 1.0f;

            if (kbdLx != 0.0f || kbdLy != 0.0f) {
                connected = true;
                // If analog stick is idle, apply WASD direction at 1.0 magnitude
                if (fabsf(lx) < 0.1f && fabsf(ly) < 0.1f) {
                    float len = sqrtf(kbdLx * kbdLx + kbdLy * kbdLy);
                    if (len > 0.0f) {
                        lx = kbdLx / len;
                        ly = kbdLy / len;
                    }
                }
            }

            // Keyboard -> pad-0 buttons so menus work without a gamepad:
            // Enter / '1' = Start, Space = Cross (OK), Backspace = Triangle
            // (cancel), arrow keys = d-pad.
            unsigned int kbdButtons = 0;
            if ((GetAsyncKeyState(VK_RETURN) & 0x8000) ||
                (GetAsyncKeyState('1') & 0x8000) ||
                (GetAsyncKeyState(VK_NUMPAD1) & 0x8000)) kbdButtons |= Start;
            if (GetAsyncKeyState(VK_SPACE)  & 0x8000) kbdButtons |= Rdown;
            if (GetAsyncKeyState(VK_BACK)   & 0x8000) kbdButtons |= Rup;
            if (sm_KeyboardDpadEnabled) {
                // Menus: arrows = d-pad (navigation).  Gameplay: arrows = the
                // face cluster, so down-arrow presses Rdown (jump) etc.
                unsigned int up    = sm_KeyboardDpadFace ? (unsigned int)Rup    : (unsigned int)Lup;
                unsigned int down  = sm_KeyboardDpadFace ? (unsigned int)Rdown  : (unsigned int)Ldown;
                unsigned int left  = sm_KeyboardDpadFace ? (unsigned int)Rleft  : (unsigned int)Lleft;
                unsigned int right = sm_KeyboardDpadFace ? (unsigned int)Rright : (unsigned int)Lright;
                if (GetAsyncKeyState(VK_UP)     & 0x8000) kbdButtons |= up;
                if (GetAsyncKeyState(VK_DOWN)   & 0x8000) kbdButtons |= down;
                if (GetAsyncKeyState(VK_LEFT)   & 0x8000) kbdButtons |= left;
                if (GetAsyncKeyState(VK_RIGHT)  & 0x8000) kbdButtons |= right;
            }
            if (kbdButtons) {
                connected = true;
                buttons |= kbdButtons;
            }
            // Remember which bits the keyboard faked so gameplay can tell them
            // from a real controller's (ioPad::GetPhysicalButtons).
            pad.SetKeyboardButtons(kbdButtons);
        } else {
            pad.SetKeyboardButtons(0);
        }

        pad.m_Connected = connected;
        pad.SetButtons(buttons);
        pad.SetNormLeft(lx, ly);
        pad.SetNormRight(rx, ry);
    }
}

#else  // !_WIN32

void ioPad::PollHardware() {}

#endif
