////////////////////////////////////////
// string.h
////////////////////////////////////////

#ifndef ATL_STRING_H
#define ATL_STRING_H

#include <string>
// AGE 2.72 game code reaches the tokenizer, streams and the asset manager
// through this header.
#include "core/stream.h"
#include "data/assetcfg.h"
#include "data/token.h"

class atString {
public:
    atString() {}
    atString(const char* s) : m_Str(s ? s : "") {}
    atString(const std::string& s) : m_Str(s) {}

    const char* Get() const { return m_Str.c_str(); }
    int GetLength() const { return (int)m_Str.length(); }

    operator const char*() const { return m_Str.c_str(); }

    atString& operator=(const char* s) {
        m_Str = (s ? s : "");
        return *this;
    }

    atString& operator+=(const char* s) {
        m_Str += (s ? s : "");
        return *this;
    }

    atString& operator+=(const atString& other) {
        m_Str += other.m_Str;
        return *this;
    }

    bool operator<(const atString& other) const {
        return m_Str < other.m_Str;
    }
    bool operator==(const atString& other) const {
        return m_Str == other.m_Str;
    }

private:
    std::string m_Str;
};

inline void atParsePathElements(const char *fullfile, atString &path, atString &file) {
    if (!fullfile) return;
    std::string s(fullfile);
    size_t last_slash = s.find_last_of("\\/");
    if (last_slash == std::string::npos) {
        path = ".";
        file = fullfile;
    } else {
        path = s.substr(0, last_slash).c_str();
        file = s.substr(last_slash + 1).c_str();
    }
}

#endif // ATL_STRING_H
