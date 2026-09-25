#ifndef GFX_STATE_H
#define GFX_STATE_H

#include "gfx/rstate.h"

// gfxState - static render-state helpers the 2.72 game code calls directly.
// Each forwards to the RSTATE instance so the two spellings stay in sync.
class gfxState {
public:
	// Scissor rectangle in screen pixels (x, y, w, h); see gfxRenderState::SetScissor.
	static void SetScissor(int x, int y, int w, int h) { RSTATE.SetScissor(x, y, w, h); }
	static void ClearScissor() { RSTATE.ClearScissor(); }
	static bool GetScissor(int &x, int &y, int &w, int &h) { return RSTATE.GetScissor(x, y, w, h); }
	static void SetGS(u64 reg, u64 val) { RSTATE.SetGS(reg, val); }
};

#endif // GFX_STATE_H
