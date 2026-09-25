#include "input/mouse.h"

static int s_X = 0;
static int s_Y = 0;
static int s_DX = 0;
static int s_DY = 0;
static int s_DZ = 0;
static unsigned int s_Buttons = 0;
static unsigned int s_ButtonsPressed = 0;
static unsigned int s_ButtonsReleased = 0;

ioMouse MOUSE;

int ioMouse::GetX() { return s_X; }
int ioMouse::GetY() { return s_Y; }
int ioMouse::GetDX() { return s_DX; }
int ioMouse::GetDY() { return s_DY; }
int ioMouse::GetDZ() { return s_DZ; }
static int s_ScreenW = 640;
static int s_ScreenH = 480;
float ioMouse::GetNormX() {
    float n = s_ScreenW > 1 ? (float)s_X / (float)(s_ScreenW - 1) : 0.0f;
    return n < 0.0f ? 0.0f : (n > 1.0f ? 1.0f : n);
}
float ioMouse::GetNormY() {
    float n = s_ScreenH > 1 ? (float)s_Y / (float)(s_ScreenH - 1) : 0.0f;
    return n < 0.0f ? 0.0f : (n > 1.0f ? 1.0f : n);
}
void ioMouse::SetScreenSize(int width, int height) {
    if (width > 0) s_ScreenW = width;
    if (height > 0) s_ScreenH = height;
}
unsigned int ioMouse::GetButtons() { return s_Buttons; }
unsigned int ioMouse::GetPressedButtons() { return s_ButtonsPressed; }
unsigned int ioMouse::GetReleasedButtons() { return s_ButtonsReleased; }

void ioMouse::SetPosition(int x, int y) {
    s_DX += (x - s_X);
    s_DY += (y - s_Y);
    s_X = x;
    s_Y = y;
}

void ioMouse::AddWheelDelta(int dz) {
    s_DZ += dz;
}

void ioMouse::SetButtonState(int button, bool down) {
    bool wasDown = (s_Buttons & button) != 0;
    if (down && !wasDown) {
        s_Buttons |= button;
        s_ButtonsPressed |= button;
    } else if (!down && wasDown) {
        s_Buttons &= ~button;
        s_ButtonsReleased |= button;
    }
}

void ioMouse::ClearEdges() {
    s_DX = 0;
    s_DY = 0;
    s_DZ = 0;
    s_ButtonsPressed = 0;
    s_ButtonsReleased = 0;
}
