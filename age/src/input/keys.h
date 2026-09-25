////////////////////////////////////////
// keys.h
////////////////////////////////////////

#ifndef INPUT_KEYS_H
#define INPUT_KEYS_H

// Keyboard key codes.  Letters/digits use their ASCII values so KEY_A..KEY_Z map
// directly; function/special keys follow.  (Backed by Win32 virtual-key codes in
// the ioKeyboard implementation.)

enum
{
	KEY_ESCAPE	= 27,
	KEY_SPACE	= 32,

	KEY_0 = '0', KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9,

	KEY_A = 'A', KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J,
	KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T,
	KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z,

	KEY_F1 = 256, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
	KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,

	KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN,
	KEY_LSHIFT, KEY_RSHIFT, KEY_LCTRL, KEY_RCTRL, KEY_ENTER, KEY_TAB,
	KEY_HOME, KEY_END,
	KEY_INSERT, KEY_DELETE, KEY_PAGEUP, KEY_PAGEDOWN,

	KEY_NUMPAD0, KEY_NUMPAD1, KEY_NUMPAD2, KEY_NUMPAD3, KEY_NUMPAD4,
	KEY_NUMPAD5, KEY_NUMPAD6, KEY_NUMPAD7, KEY_NUMPAD8, KEY_NUMPAD9,
	KEY_SUBTRACT, KEY_ADD, KEY_MULTIPLY, KEY_DIVIDE, KEY_DECIMAL,

	KEY_ALT, KEY_MINUS, KEY_BACK,
	KEY_LALT, KEY_RALT,
	KEY_COMMA, KEY_PERIOD, KEY_EQUALS, KEY_SLASH, KEY_BACKSLASH,
	KEY_SEMICOLON, KEY_APOSTROPHE, KEY_LBRACKET, KEY_RBRACKET, KEY_GRAVE,
	KEY_CAPSLOCK, KEY_NUMLOCK, KEY_SCROLL, KEY_PAUSE, KEY_PRINT,

	KEY_MAX,

	// Aliases.  These sit after KEY_MAX on purpose: an alias mid-enum
	// rewinds the implicit counter, which had KEY_NUMPAD0 colliding with
	// KEY_RSHIFT.
	KEY_CONTROL = KEY_LCTRL,
	KEY_LCONTROL = KEY_LCTRL,
	KEY_RCONTROL = KEY_RCTRL,
	KEY_SHIFT = KEY_LSHIFT,
	KEY_RETURN = KEY_ENTER,
	KEY_NUMPADENTER = KEY_ENTER,   // numpad enter is not distinguished on the PC keyboard path
	KEY_LMENU = KEY_LALT,
	KEY_RMENU = KEY_RALT
};

#endif // INPUT_KEYS_H
