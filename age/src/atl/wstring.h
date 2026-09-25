#ifndef ATL_WSTRING_H
#define ATL_WSTRING_H

////////////////////////////////////////
// atl/wstring.h
//
// Wide/narrow string conversion helpers.  W2A/A2W return a pointer into a
// small ring of static buffers, so the result is valid for the duration of
// the enclosing expression (the usual formatf(...W2A(s)...) pattern).
// _TCHAR and the _tcs* functions come from data/unicode.h (wide).
////////////////////////////////////////

#include "data/unicode.h"
#include <wchar.h>
#include <string>
#include <string.h>

// ATL-style declaration the game places before W2A/A2W use; the ring
// buffers here need no per-scope state.
#define USES_CONVERSION

const char *W2A(const wchar_t *src);
const char *W2A(const unsigned short *src);
wchar_t *A2W(const char *src);

// Copying conversions for callers that keep the result.
int atWideToAscii(char *dest, int maxLen, const wchar_t *src);
int atAsciiToWide(wchar_t *dest, int maxLen, const char *src);

class atWideString {
public:
    atWideString() {}
    atWideString(const wchar_t* s) : m_Str(s ? s : L"") {}
    atWideString(const unsigned short* s) : m_Str(s ? (const wchar_t*)s : L"") {}
    atWideString(const char* s) {
        if (s) {
            int len = (int)strlen(s);
            m_Str.resize(len);
            for (int i = 0; i < len; ++i) {
                m_Str[i] = (wchar_t)(unsigned char)s[i];
            }
        }
    }
    atWideString(const std::wstring& s) : m_Str(s) {}

    const wchar_t* Get() const { return m_Str.c_str(); }
    int GetLength() const { return (int)m_Str.length(); }
    int Length() const { return (int)m_Str.length(); }
    bool IsEmpty() const { return m_Str.empty(); }

    operator const wchar_t*() const { return m_Str.c_str(); }
    operator const unsigned short*() const { return (const unsigned short*)m_Str.c_str(); }
    operator const void*() const { return m_Str.c_str(); }

    atWideString& operator=(const wchar_t* s) {
        m_Str = (s ? s : L"");
        return *this;
    }
    atWideString& operator=(const unsigned short* s) {
        m_Str = (s ? (const wchar_t*)s : L"");
        return *this;
    }
    atWideString& operator=(const char* s) {
        m_Str.clear();
        if (s) {
            int len = (int)strlen(s);
            m_Str.resize(len);
            for (int i = 0; i < len; ++i) {
                m_Str[i] = (wchar_t)(unsigned char)s[i];
            }
        }
        return *this;
    }

    atWideString& operator+=(const wchar_t* s) {
        m_Str += (s ? s : L"");
        return *this;
    }

    atWideString& operator+=(const atWideString& other) {
        m_Str += other.m_Str;
        return *this;
    }

    bool operator<(const atWideString& other) const {
        return m_Str < other.m_Str;
    }
    bool operator==(const atWideString& other) const {
        return m_Str == other.m_Str;
    }
    bool operator!=(const atWideString& other) const {
        return m_Str != other.m_Str;
    }

private:
    std::wstring m_Str;
};

#endif // ATL_WSTRING_H
