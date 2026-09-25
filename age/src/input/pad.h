#include "core/output.h"
#ifndef INPUT_PAD_H
#define INPUT_PAD_H

// Zero an axis value inside +/-limit (AGE 2.72 keeps this in pad.h so that
// game code including only input/pad.h can use it).
inline float ioAddDeadZone(float val, float limit) {
    if (val > -limit && val < limit) return 0.0f;
    return val;
}

// Which actuator a rumble value addresses.  This belongs here, not in
// veh_base/feedback.h where the port had it: actuators are a pad concept and
// the alpha has ioPad::SetActuatorValue / SetActuatorsActivation on the pad
// itself.  kLeft/kRight are the wheel backend's names for the same two.
enum eFeedbackActuator {
    kSmall = 0,
    kLarge = 1,
    kLeft = 0,
    kRight = 1
};

class ioPad {
public:
    // Bit layout is LOAD-BEARING: control.map button names resolve through
    // bhPadInputInfo::smButtonStrings by INDEX (1 << i), so these values must
    // stay in the original AGE order (L2,R2,L1,R1, Rup..Rleft, Select,L3,R3,
    // Start, Lup..Lleft).  A different order swaps the L/R clusters for every
    // data-driven binding even though compiled code (symbolic names) agrees.
    enum Button {
        L2       = 0x0001,
        R2       = 0x0002,
        L1       = 0x0004,
        R1       = 0x0008,
        Rup      = 0x0010,
        Rright   = 0x0020,
        Rdown    = 0x0040,
        Rleft    = 0x0080,
        Select   = 0x0100,
        L3       = 0x0200,
        R3       = 0x0400,
        Start    = 0x0800,
        Lup      = 0x1000,
        Lright   = 0x2000,
        Ldown    = 0x4000,
        Lleft    = 0x8000
    };

    ioPad();
    ~ioPad();

    unsigned int GetButtons() const;

    // The keyboard doubles as pad 0 so the frontend is usable without a
    // controller (PollHardware below).  Those synthesized bits are a MENU
    // convenience only: gameplay must not read space as the gas button just
    // because space is the menu's OK.  GetButtons() keeps them (the UI wants
    // them); GetPhysicalButtons() is what a real controller is holding, and is
    // what driving code and the control mapper read.
    unsigned int GetKeyboardButtons() const { return m_KeyboardButtons; }
    unsigned int GetPhysicalButtons() const { return m_Buttons & ~m_KeyboardButtons; }
    void SetKeyboardButtons(unsigned int buttons) { m_KeyboardButtons = buttons; }

    unsigned int GetPressedButtons() const;
    unsigned int GetReleasedButtons() const;
    unsigned int GetChangedButtons() const { return GetPressedButtons() | GetReleasedButtons(); }
    unsigned int GetPressedDebugButtons() const { return (m_Buttons & Select) ? (GetPressedButtons() & ~Select) : 0; }
    unsigned int GetDebugButtons() const {
        // AGE debug modifier: holding Select (Back on Xbox) while pressing other buttons
        // yields debug button presses (e.g. Select + Rup -> Reset, Select + Rleft -> Restart).
        return (m_Buttons & Select) ? (m_Buttons & ~Select) : 0;
    }

    float GetNormRightX() const;
    float GetNormRightY() const;
    float GetNormLeftX() const;
    float GetNormLeftY() const;
    float GetLeftX() const { return GetNormLeftX(); }
    float GetLeftY() const { return GetNormLeftY(); }

    // Instance setters for simulation/mapping
    void SetButtons(unsigned int buttons);
    void SetNormLeft(float lx, float ly);
    void SetNormRight(float rx, float ry);
    void ClearEdges();

    // Static interface
    static ioPad & GetPad(int index);
    static void SetUseKeymap(bool use);
    static bool IsMultitapConnected(int port);
    static void SetSupportMultitap(bool support);

    // Poll physical controllers (XInput on Win32) into the 4 pad slots. Called
    // once per frame from the universal input point (ageExit). Reads edges for
    // this frame, so GetPressedButtons/GetReleasedButtons work as expected.
    static void PollHardware();
    static bool IsConnected(int index);
    bool IsConnected() const { return IsConnected((int)(this - sm_Pads)); }   // this pad slot

    // Keyboard arrow keys double as pad-0 d-pad for menu navigation.  The
    // editor claims the arrows for its own controls, so its build defaults
    // this off (pad.cpp); toggle at runtime if a mode needs it back.
    static void SetKeyboardDpadEnabled(bool enable);

    // Which cluster the arrows alias while enabled.  Gameplay wants the face
    // buttons (down-arrow = Rdown = jump, matching the original PC feel);
    // menus want the d-pad for navigation.  gmGame::Update flips this with
    // the UI-active state each frame.
    static void SetKeyboardDpadCluster(bool faceButtons);

private:
    static bool sm_KeyboardDpadEnabled;
    static bool sm_KeyboardDpadFace;
public:

    // Rumble.  The DualShock2 has two actuators: 0 (kSmall) is on/off, 1
    // (kLarge) takes 0..255.  XInput's right motor is the small/high-frequency
    // one and the left motor the large/low-frequency one, so they map across
    // directly (pad.cpp).  Values are held per pad and re-sent on activation.
    // value is NORMALIZED 0..1 and clamped, as on the console
    // (ioPad::SetActuatorValue, 0x22efe8: clamps to [0,1], then actuator 0 is
    // (int)value - the small motor is on/off - and actuator 1 scales by 255).
    void SetActuatorValue(int actuator, float value);
    void SetActuatorsActivation(bool active);
    void PushRumble();

private:
    unsigned int m_Buttons;
    unsigned int m_KeyboardButtons;   // subset of m_Buttons synthesized from the keyboard
    unsigned int m_ButtonsPressed;
    unsigned int m_ButtonsReleased;

    float m_LeftX;
    float m_LeftY;
    float m_RightX;
    float m_RightY;

    bool m_Connected;
    unsigned short m_Motor[2] = { 0, 0 };   // [0] = small/right, [1] = large/left
    bool m_ActuatorsActive = true;

    static ioPad sm_Pads[4];
    static bool sm_UseKeymap;
    static bool sm_SupportMultitap;
};

#endif // INPUT_PAD_H
