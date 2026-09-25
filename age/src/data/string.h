////////////////////////////////////////
// string.h
////////////////////////////////////////

#ifndef DATA_STRING_H
#define DATA_STRING_H

#include <string.h>
#include <stdlib.h>
#include <string>		// ConstString has a std::string constructor

#ifndef NULL
#define NULL 0
#endif

class ConstString {
public:
    ConstString() : m_String(NULL) {}
    ConstString(const char* s) {
        m_String = s ? _strdup(s) : NULL;
    }
    ConstString(const std::string &s) {
        m_String = _strdup(s.c_str());
    }
    ~ConstString() {
        if (m_String) free((void*)m_String);
    }
    ConstString(const ConstString& other) {
        m_String = other.m_String ? _strdup(other.m_String) : NULL;
    }
    ConstString(ConstString&& other) noexcept : m_String(other.m_String) {
        other.m_String = NULL;
    }
    ConstString& operator=(const ConstString& other) {
        if (this != &other) {
            if (m_String) free((void*)m_String);
            m_String = other.m_String ? _strdup(other.m_String) : NULL;
        }
        return *this;
    }
    ConstString& operator=(ConstString&& other) noexcept {
        if (this != &other) {
            if (m_String) free((void*)m_String);
            m_String = other.m_String;
            other.m_String = NULL;
        }
        return *this;
    }
    ConstString& operator=(const char* s) {
        if (m_String) free((void*)m_String);
        m_String = s ? _strdup(s) : NULL;
        return *this;
    }

    operator const char*() const { return m_String ? m_String : ""; }
    const char* Get() const { return m_String ? m_String : ""; }

    bool operator<(const ConstString& other) const {
        const char* s1 = m_String ? m_String : "";
        const char* s2 = other.m_String ? other.m_String : "";
        return strcmp(s1, s2) < 0;
    }

    bool operator==(const ConstString& other) const {
        const char* s1 = m_String ? m_String : "";
        const char* s2 = other.m_String ? other.m_String : "";
        return strcmp(s1, s2) == 0;
    }

public:   // the game reads the raw pointer directly (ConstString::m_String)
    const char* m_String;
};

#ifndef CONSTSTRINGDUPLICATE_DEFINED
#define CONSTSTRINGDUPLICATE_DEFINED
inline const char* ConstStringDuplicate(const char* s) {
    return s ? _strdup(s) : NULL;
}
#endif

inline void StringNormalize(char* dest, const char* src) {
    if (!dest) return;
    if (!src) {
        dest[0] = '\0';
        return;
    }
    int i = 0;
    for (; src[i] != '\0'; i++) {
        char c = src[i];
        if (c >= 'A' && c <= 'Z') {
            c = c - 'A' + 'a';
        } else if (c == '\\') {
            c = '/';
        }
        dest[i] = c;
    }
    dest[i] = '\0';
}

#include <stdarg.h>
#include <stdio.h>

// POSIX spellings used by the game code.
#ifndef strcasecmp
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#endif

inline char* StringDuplicate(const char *s) {
    return s ? _strdup(s) : NULL;
}

inline void StringFree(const char* s) {
    if (s) free((void*)s);
}

inline const char* formatf(const char *fmt, ...) {
    static char s_FormatBuf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(s_FormatBuf, sizeof(s_FormatBuf), fmt, args);
    va_end(args);
    return s_FormatBuf;
}

#endif // DATA_STRING_H
