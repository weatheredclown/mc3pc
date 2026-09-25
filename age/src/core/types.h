////////////////////////////////////////
// types.h
////////////////////////////////////////

#ifndef CORE_TYPES_H
#define CORE_TYPES_H

#define _ALLOW_KEYWORD_MACROS

// types.h is force-included on every TU (via /FI opnew.h), so it must NOT drag in
// <windows.h>: that leaked Win32's dialog control-ID macros (frm1/edt1/cmb1/...
// from dlgs.h) into game code that uses those names as identifiers — e.g.
// crAnimFrame::Blend(float, const crAnimFrame &frm1, const crAnimFrame &frm2).
// This header uses nothing from windows.h; TUs that need Win32 include it directly.
#include <float.h>
#include <string.h>
#include <stdlib.h>

#ifndef WIN32PC_ONLY
#define WIN32PC_ONLY(x) x
#endif

#ifndef ALIGNED
#define ALIGNED(n)
#endif

#ifndef M_SQRT2
#define M_SQRT2 1.41421356237309504880
#endif

#ifndef M_E
#define M_E 2.71828182845904523536
#endif

typedef unsigned char		u8;
typedef unsigned short		u16;
typedef unsigned int		u32;
typedef unsigned long long	u64;

typedef signed char			s8;
typedef signed short		s16;
typedef signed int			s32;
typedef signed long long	s64;

typedef u8					uint8;
typedef u16					uint16;
typedef u32					uint32;
typedef u64					uint64;

typedef s8					int8;
typedef s16					int16;
typedef s32					int32;
typedef s64					int64;

typedef float				f32;
typedef double				f64;

#ifndef NULL
#define NULL 0
#endif

#ifndef SMALL_FLOAT
#define SMALL_FLOAT 1.0e-37f
#endif

#ifndef LARGE_FLOAT
#define LARGE_FLOAT 1.0e37f
#endif

#ifndef CONSTSTRINGDUPLICATE_DEFINED
#define CONSTSTRINGDUPLICATE_DEFINED
inline const char* ConstStringDuplicate(const char* s) {
    return s ? _strdup(s) : NULL;
}
#endif

unsigned int ipcTime();

inline float PH_MPH2MPS(float mph) { return mph * 0.44704f; }
inline float PH_MPS2MPH(float mps) { return mps * 2.2369363f; }

union flint {
    float f;
    int i;
    flint() : i(0) {}
    flint(float _f) : f(_f) {}
    flint(int _i) : i(_i) {}
    operator int() const { return (int)f; }
    operator float() const { return f; }
};

inline void Prefetch(const void *ptr) {}

template <typename T>
inline T square(const T &x) {
    return x * x;
}

// Marks a parameter only used by the paging (resource streaming) build.
#ifndef IS_CONSOLE
#define IS_CONSOLE 0
#endif

#ifndef PAGING_ONLY
#define PAGING_ONLY(x) x
#endif
// Platform-only parameter markers (the PC build is neither).
#ifndef XBOX_ONLY
#define XBOX_ONLY(x)
#endif
#ifndef PS2_ONLY
#define PS2_ONLY(x)
#endif

template <typename T>
inline T Clamp(T val, T minVal, T maxVal) {
    if (val < minVal) return minVal;
    if (val > maxVal) return maxVal;
    return val;
}
template <typename T>
inline T ClampRange(T val, T minVal, T maxVal) {
    if (maxVal <= minVal) return static_cast<T>(0);
    T t = (val - minVal) / (maxVal - minVal);
    return t < static_cast<T>(0) ? static_cast<T>(0) : (t > static_cast<T>(1) ? static_cast<T>(1) : t);
}

// Non-template forms so mixed int/enum arguments resolve.
inline int Clamp(int val, int minVal, int maxVal) { return val < minVal ? minVal : (val > maxVal ? maxVal : val); }
inline float Clamp(float val, float minVal, float maxVal) { return val < minVal ? minVal : (val > maxVal ? maxVal : val); }
inline float ClampRange(float val, float minVal, float maxVal) {
    if (maxVal <= minVal) return 0.0f;
    float t = (val - minVal) / (maxVal - minVal);
    return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
}
inline float Lerp(float t, float a, float b) {
    return a + t * (b - a);
}
template<class T> inline T Max(T a, T b) { return a > b ? a : b; }
template<class T> inline T Min(T a, T b) { return a < b ? a : b; }
template<class T> inline T Max(T a, T b, T c) { return Max(Max(a, b), c); }
template<class T> inline T Min(T a, T b, T c) { return Min(Min(a, b), c); }
template<class T> inline T Max(T a, T b, T c, T d) { return Max(Max(a, b), Max(c, d)); }
template<class T> inline T Min(T a, T b, T c, T d) { return Min(Min(a, b), Min(c, d)); }
// Mixed-type forms (int vs enum, float vs int ...): the same-type templates
// above are more specialised, so they still win when both arguments match.
template<class T, class U> inline auto Max(T a, U b) -> decltype(a > b ? a : b) { return a > b ? a : b; }
template<class T, class U> inline auto Min(T a, U b) -> decltype(a < b ? a : b) { return a < b ? a : b; }

#if __DEV
#define DEV_ONLY(x) x
#else
#define DEV_ONLY(x)
#endif

#if __BANK
#define BANK_ONLY(x) x
#else
#define BANK_ONLY(x)
#endif

#if __PSX2
#define PSX2_ONLY(x) x
#else
#define PSX2_ONLY(x)
#endif

// PS2 scratchpad placement attribute.  On PS2 this pins a static to the fast
// 16KB scratchpad; every other target (incl. DX11) keeps it in normal memory.
#ifndef IN_SCRATCHPAD
#define IN_SCRATCHPAD
#endif

#ifndef VUDATA
#define VUDATA(type, name) type name
#endif

// PS2 EE hardware/toolchain compatibility: the PS2_NATIVE gate, a portable u128,
// and asm() neutralisation so the __PSX2 code paths build on MSVC/PC.  Kept in
// its own header to keep the PS2 bridge in one place.  Included here (not at the
// top) because its u128 union needs the u8..f32 typedefs above.
#include "core/ps2compat.h"

#ifndef BIT
#define BIT(n) (1u << (n))
#endif

// Little-endian FourCC (matches the on-disk format: bytes a,b,c,d read as a u32
// LE — e.g. 'a','n','i',0 -> 0x00696E61, per the Rust Oni2Rebuilt parser).
#ifndef MAKE_MAGIC_NUMBER
#define MAKE_MAGIC_NUMBER(a,b,c,d) \
	( ((u32)(u8)(a)) | ((u32)(u8)(b)<<8) | ((u32)(u8)(c)<<16) | ((u32)(u8)(d)<<24) )
#endif

// Exchange two values in place (used e.g. to mirror left/right channel data).
template<class T> inline void SwapEm(T &a, T &b) { T t=a; a=b; b=t; }

#include <malloc.h>
#define Alloca(T, n) ((T*)_alloca(sizeof(T)*(n)))

#ifndef CompileTimeAssert
#define CompileTimeAssert(cond) static_assert(cond, "CompileTimeAssert failed")
#endif

#ifndef TRed
#define TRed "\x1b[31m"
#endif
#ifndef TGreen
#define TGreen "\x1b[32m"
#endif
#ifndef TBlue
#define TBlue "\x1b[34m"
#endif
#ifndef TYellow
#define TYellow "\x1b[33m"
#endif
#ifndef TPurple
#define TPurple "\x1b[35m"
#endif
#ifndef TCyan
#define TCyan "\x1b[36m"
#endif
#ifndef TNorm
#define TNorm "\x1b[0m"
#endif

#include <stdio.h>

#pragma warning(push)
#pragma warning(disable: 4455) // literal suffix identifier is reserved
#pragma warning(disable: 4599) // literal suffix warning
inline const char* operator ""TRed(const char* str, size_t len) {
    static char buffers[16][256]; static int idx = 0; char* b = buffers[idx]; idx = (idx + 1) % 16;
    snprintf(b, 256, "%.*s\x1b[31m", (int)len, str); return b;
}
inline const char* operator ""TGreen(const char* str, size_t len) {
    static char buffers[16][256]; static int idx = 0; char* b = buffers[idx]; idx = (idx + 1) % 16;
    snprintf(b, 256, "%.*s\x1b[32m", (int)len, str); return b;
}
inline const char* operator ""TBlue(const char* str, size_t len) {
    static char buffers[16][256]; static int idx = 0; char* b = buffers[idx]; idx = (idx + 1) % 16;
    snprintf(b, 256, "%.*s\x1b[34m", (int)len, str); return b;
}
inline const char* operator ""TYellow(const char* str, size_t len) {
    static char buffers[16][256]; static int idx = 0; char* b = buffers[idx]; idx = (idx + 1) % 16;
    snprintf(b, 256, "%.*s\x1b[33m", (int)len, str); return b;
}
inline const char* operator ""TPurple(const char* str, size_t len) {
    static char buffers[16][256]; static int idx = 0; char* b = buffers[idx]; idx = (idx + 1) % 16;
    snprintf(b, 256, "%.*s\x1b[35m", (int)len, str); return b;
}
inline const char* operator ""TCyan(const char* str, size_t len) {
    static char buffers[16][256]; static int idx = 0; char* b = buffers[idx]; idx = (idx + 1) % 16;
    snprintf(b, 256, "%.*s\x1b[36m", (int)len, str); return b;
}
inline const char* operator ""TNorm(const char* str, size_t len) {
    static char buffers[16][256]; static int idx = 0; char* b = buffers[idx]; idx = (idx + 1) % 16;
    snprintf(b, 256, "%.*s\x1b[0m", (int)len, str); return b;
}
#pragma warning(pop)

inline void* aligned_new(size_t size, size_t alignment) {
    return _aligned_malloc(size, alignment);
}
inline void aligned_delete(void* ptr) {
    _aligned_free(ptr);
}

#ifndef BIT0
#define BIT0  (1u << 0)
#define BIT1  (1u << 1)
#define BIT2  (1u << 2)
#define BIT3  (1u << 3)
#define BIT4  (1u << 4)
#define BIT5  (1u << 5)
#define BIT6  (1u << 6)
#define BIT7  (1u << 7)
#define BIT8  (1u << 8)
#define BIT9  (1u << 9)
#define BIT10 (1u << 10)
#define BIT11 (1u << 11)
#define BIT12 (1u << 12)
#define BIT13 (1u << 13)
#define BIT14 (1u << 14)
#define BIT15 (1u << 15)
#define BIT16 (1u << 16)
#define BIT17 (1u << 17)
#define BIT18 (1u << 18)
#define BIT19 (1u << 19)
#define BIT20 (1u << 20)
#define BIT21 (1u << 21)
#define BIT22 (1u << 22)
#define BIT23 (1u << 23)
#define BIT24 (1u << 24)
#define BIT25 (1u << 25)
#define BIT26 (1u << 26)
#define BIT27 (1u << 27)
#define BIT28 (1u << 28)
#define BIT29 (1u << 29)
#define BIT30 (1u << 30)
#define BIT31 (1u << 31)
#endif

#endif // CORE_TYPES_H
