#include "core/output.h"
#ifndef INPUT_MOUSE_H
#define INPUT_MOUSE_H

class ioMouse
{
public:
    enum Button {
        mouseLeft = 0x01,
        mouseRight = 0x02,
        mouseMiddle = 0x04,
        mouseExtraBtn1 = 0x08,   // XButton1.. (mapper bindings)
        mouseExtraBtn2 = 0x10,
        mouseExtraBtn3 = 0x20,
        mouseExtraBtn4 = 0x40,
        mouseExtraBtn5 = 0x80
    };

    static int GetX();
    static int GetY();
    static int GetDX();
    static int GetDY();
    static int GetDZ();
    // Cursor position normalized to the client area: 0..1 left->right, top->bottom.
    static float GetNormX();
    static float GetNormY();
    static unsigned int GetButtons();
    static unsigned int GetPressedButtons();
    static unsigned int GetReleasedButtons();
    // Buttons whose state differs from the previous update.
    static unsigned int GetChangedButtons() { return GetPressedButtons() | GetReleasedButtons(); }
    // PC mice report a wheel; consoles' USB mice may not.
    static bool HasWheel() { return true; }
    static void SetUSBMouseSupport(bool support) { Quitf("ioMouse::SetUSBMouseSupport - not implemented"); }

    // Engine-internal feeding interface
    static void SetPosition(int x, int y);
    // Client-area size used by GetNormX/Y (fed by the window proc).
    static void SetScreenSize(int width, int height);
    static void AddWheelDelta(int dz);
    static void SetButtonState(int button, bool down);
    static void ClearEdges();
};

extern ioMouse MOUSE;

#endif // INPUT_MOUSE_H
