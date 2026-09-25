#include "input/keyboard.h"
#include <string.h>

static bool s_KeyDown[KEY_MAX] = { false };
static bool s_KeyPressed[KEY_MAX] = { false };
static bool s_KeyReleased[KEY_MAX] = { false };

ioKeyboard KEYBOARD;

bool ioKeyboard::KeyPressed(int key) {
    if (key < 0 || key >= KEY_MAX) return false;
    return s_KeyPressed[key];
}

bool ioKeyboard::KeyDown(int key) {
    if (key < 0 || key >= KEY_MAX) return false;
    return s_KeyDown[key];
}

bool ioKeyboard::KeyReleased(int key) {
    if (key < 0 || key >= KEY_MAX) return false;
    return s_KeyReleased[key];
}

void ioKeyboard::SetKeyDown(int key, bool down) {
    if (key < 0 || key >= KEY_MAX) return;
    if (s_KeyDown[key] != down) {
        s_KeyDown[key] = down;
        if (down) {
            s_KeyPressed[key] = true;
        } else {
            s_KeyReleased[key] = true;
        }
    }
}

void ioKeyboard::ClearEdges() {
    memset(s_KeyPressed, 0, sizeof(s_KeyPressed));
    memset(s_KeyReleased, 0, sizeof(s_KeyReleased));
}

int ioKeyboard::GetBufferedInput()
{
    for (int k = 1; k < KEY_MAX; k++)
        if (KeyPressed(k)) return k;
    return 0;
}

int ioKeyboard::GetBufferedInput(char *dest, int maxLen)
{
    int n = 0;
    if (!dest || maxLen <= 0) return 0;
    for (int k = 1; k < KEY_MAX && n < maxLen - 1; k++)
        if (KeyPressed(k)) dest[n++] = (char)k;
    dest[n] = 0;
    return n;
}
