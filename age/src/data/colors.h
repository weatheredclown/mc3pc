#ifndef DATA_COLORS_H
#define DATA_COLORS_H

#include "vector/vector3.h"

#define DEF_COLOR(name, r, g, b) static const Vector3 name(r, g, b)

DEF_COLOR(Color_white, 1.0f, 1.0f, 1.0f);
DEF_COLOR(Color_black, 0.0f, 0.0f, 0.0f);
DEF_COLOR(Color_red, 1.0f, 0.0f, 0.0f);
DEF_COLOR(Color_green, 0.0f, 1.0f, 0.0f);
DEF_COLOR(Color_blue, 0.0f, 0.0f, 1.0f);
DEF_COLOR(Color_yellow, 1.0f, 1.0f, 0.0f);
DEF_COLOR(Color_magenta, 1.0f, 0.0f, 1.0f);
DEF_COLOR(Color_cyan, 0.0f, 1.0f, 1.0f);
DEF_COLOR(Color_grey, 0.5f, 0.5f, 0.5f);
DEF_COLOR(Color_gray, 0.5f, 0.5f, 0.5f);
DEF_COLOR(Color_pink, 1.0f, 0.75f, 0.8f);
DEF_COLOR(Color_red2, 0.8f, 0.1f, 0.1f);
DEF_COLOR(Color_yellow3, 1.0f, 1.0f, 0.3f);
DEF_COLOR(Color_DodgerBlue, 0.12f, 0.56f, 1.0f);
DEF_COLOR(Color_DodgerBlue2, 0.1f, 0.5f, 0.9f);
DEF_COLOR(Color_azure3, 0.76f, 0.82f, 0.82f);
DEF_COLOR(Color_orange2, 1.0f, 0.55f, 0.0f);
DEF_COLOR(Color_salmon3, 0.8f, 0.47f, 0.42f);
DEF_COLOR(Color_LightYellow, 1.0f, 1.0f, 0.8f);

#undef DEF_COLOR

#endif // DATA_COLORS_H
