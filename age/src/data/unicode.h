#ifndef DATA_UNICODE_H
#define DATA_UNICODE_H

////////////////////////////////////////
// data/unicode.h
//
// AGE's text character type.  Game strings (string tables, player names,
// fonts) are wide; _TCHAR / _T() / _tcs* follow that convention on every
// platform, independent of the compiler's <tchar.h> setting.
////////////////////////////////////////

#include <wchar.h>

#ifdef _TCHAR
#undef _TCHAR
#endif
typedef wchar_t _TCHAR;

#ifdef _T
#undef _T
#endif
#define _T(x) L##x

#ifndef AGE_TCS_DEFINED
#define AGE_TCS_DEFINED
#define _tcslen   wcslen
#define _tcsdup   _wcsdup
#define _tcscmp   wcscmp
#define _tcsncmp  wcsncmp
#define _tcsicmp  _wcsicmp
#define _tcscpy   wcscpy
#define _tcsncpy  wcsncpy
#define _tcscat   wcscat
#define _tcschr   wcschr
#define _tcsrchr  wcsrchr
#define _tcsstr   wcsstr
#define _stprintf swprintf
#define _sntprintf _snwprintf
#define _tcstol   wcstol
#endif

inline _TCHAR* _TStringDuplicate(const _TCHAR *s) {
    if (!s) return nullptr;
    size_t len = wcslen(s);
    _TCHAR *p = new _TCHAR[len + 1];
    wcscpy(p, s);
    return p;
}

#endif // DATA_UNICODE_H
