#ifndef INPUT_KEYBOARD_H
#define INPUT_KEYBOARD_H

#include "input/keys.h"

class ioKeyboard
{
public:
    static bool KeyPressed(int key);
    static bool KeyDown(int key);
    static bool KeyReleased(int key);
    // Next key from the typed-character buffer (0 when empty).
    static int GetBufferedInput();
    // Drain the typed characters into dest (NUL-terminated); returns the count.
    static int GetBufferedInput(char *dest, int maxLen);

    // Engine-internal feeding interface
    static void SetKeyDown(int key, bool down);
    static void ClearEdges();
};

extern ioKeyboard KEYBOARD;

#endif // INPUT_KEYBOARD_H
