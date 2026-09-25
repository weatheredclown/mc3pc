////////////////////////////////////////
// ps2compat.h
//
// PS2 EE hardware / toolchain compatibility shim.  Included by core/types.h
// (after the base integer typedefs u8..f32, which the u128 union below needs).
// This lets the feature-complete __PSX2 code paths compile on a non-PS2
// toolchain (MSVC / PC) WITHOUT editing the PS2 sources themselves.
//
//   __PSX2      selects the (feature-complete) PS2 *code paths*.  Stays on.
//   PS2_NATIVE  is true only when actually building for the EE core with its GCC
//               (128-bit registers, MMI SIMD, scratchpad, GNU inline asm).  On
//               PC it is 0, and the shims below take over.
////////////////////////////////////////

#ifndef CORE_PS2COMPAT_H
#define CORE_PS2COMPAT_H

#ifndef PS2_NATIVE
#  if __PSX2 && (defined(__R5900__) || defined(__MIPSEL__))
#    define PS2_NATIVE 1
#  else
#    define PS2_NATIVE 0
#  endif
#endif

#if !PS2_NATIVE

// Portable stand-in for the EE 128-bit quadword.  It overlays the packed views
// the MMI code manipulates (bytes / halfwords / words / floats).  The implicit
// int constructor mirrors the EE, where a scalar seed (e.g. pextlh(t)) lands in
// the low word with the rest zero.
union u128 {
    u8  ub[16]; s8  sb[16];
    u16 uh[8];  s16 sh[8];
    u32 uw[4];  s32 sw[4];
    f32 f[4];
    u128() {}
    u128(int v) { sw[0] = v; sw[1] = 0; sw[2] = 0; sw[3] = 0; }
};

// GNU inline asm (EE MMI) can't be parsed by MSVC.  The only raw asm() in the
// tree is the PS2 SIMD helper block in cranimation/frame.cpp, and it is DEAD on
// PC: its callers live behind #if FIXED_POINT (== 0 here), so those helpers are
// compiled but never invoked.  Neutralising asm() lets their bodies parse (they
// return an uninitialised u128, which is fine precisely because nothing calls
// them).  NOTE: this makes the code COMPILE, not run correctly — if FIXED_POINT
// is ever enabled for a PC build these helpers must be given real emulations.
#if defined(_MSC_VER)
#define asm(...)
#endif

#endif // !PS2_NATIVE

#endif // CORE_PS2COMPAT_H
