////////////////////////////////////////
// token.h
////////////////////////////////////////

#ifndef DATA_TOKEN_H
#define DATA_TOKEN_H

#include "core/output.h"
#include "core/types.h"
#include "core/stream.h"
#include "vector/vector2.h"
#include "vector/vector3.h"
#include "vector/Vector4.h"

class datBaseTokenizer {
public:
    char filename[128];
    int line;
    char CommentChar;

    datBaseTokenizer() : line(1), CommentChar('#') { filename[0] = '\0'; }
    virtual ~datBaseTokenizer() {}
    virtual void GetDelimiter(const char *delim) { Quitf("datBaseTokenizer::GetDelimiter - not implemented"); }
    virtual void MatchVector(const char *token, class Vector2 &v) { Quitf("datBaseTokenizer::MatchVector - not implemented"); }
    virtual void MatchVector(const char *token, class Vector3 &v) { Quitf("datBaseTokenizer::MatchVector - not implemented"); }
    virtual void MatchVector(const char *token, class Vector4 &v) { Quitf("datBaseTokenizer::MatchVector - not implemented"); }
    virtual void MatchIVector(const char *token, class Vector3 &v) { Quitf("datBaseTokenizer::MatchIVector - not implemented"); }
    virtual void MatchIVector(const char *token, class Vector4 &v) { Quitf("datBaseTokenizer::MatchIVector - not implemented"); }
    virtual void PutStr(const char *fmt, ...) { Quitf("datBaseTokenizer::PutStr - not implemented"); }
    virtual void Put(const char *str) { Quitf("datBaseTokenizer::Put - not implemented"); }
    virtual void Put(const class Vector3 &v) { Quitf("datBaseTokenizer::Put - not implemented"); }
    virtual void Put(const class Vector4 &v) { Quitf("datBaseTokenizer::Put - not implemented"); }
    virtual void Put(int val) { Quitf("datBaseTokenizer::Put - not implemented"); }
    virtual void Put(float val) { Quitf("datBaseTokenizer::Put - not implemented"); }
    virtual int GetToken(char *buffer, int maxLen) { return 0; }
    virtual int MatchInt(const char* token) { return 0; }
    virtual float MatchFloat(const char* token) { return 0.0f; }
    virtual int MatchIInt(const char* token) { return 0; }
    virtual float MatchIFloat(const char* token) { return 0.0f; }
    virtual void MatchToken(const char *token) { Quitf("datBaseTokenizer::MatchToken - not implemented"); }
    virtual void MatchIToken(const char *token) { Quitf("datBaseTokenizer::MatchIToken - not implemented"); }
    virtual void Pop() { Quitf("datBaseTokenizer::Pop - not implemented"); }
    int GetLine() const { return line; }
    // Rest of the current line into buffer; returns its length (0 at end of stream).
    virtual int GetLine(char *buffer, int maxLen) { return 0; }
    virtual bool CheckIToken(const char *token, bool consume = true) { return false; }
    virtual float GetFloat() { return 0.0f; }
    virtual int GetInt() { return 0; }
    virtual bool CheckToken(const char *token, bool consume = true) { return false; }
    virtual void PutDelimiter(const char *str) { Quitf("datBaseTokenizer::PutDelimiter - not implemented"); }
    virtual void GetVector(class Vector3 &out) { Quitf("datBaseTokenizer::GetVector - not implemented"); }
    virtual void GetVector(class Vector4 &out) { Quitf("datBaseTokenizer::GetVector - not implemented"); }
    virtual bool SkipCommentChars() { return false; }
};

class datAsciiTokenizer : public datBaseTokenizer {
public:
    datAsciiTokenizer();
    virtual ~datAsciiTokenizer();

    void Init(const char *name, Stream *stream);
    void Init(void);

    bool CheckToken(const char *token, bool consume = true);
    void MatchToken(const char *token);
    // Leaves the current block: skips whatever the loader did not read
    // (including nested blocks) up to and past the matching '}'.
    void Pop();
    bool CheckIToken(const char *token, bool consume = true) { return CheckToken(token, consume); }
    void MatchIToken(const char *token) { MatchToken(token); }
    void GetDelimiter(const char *delim);
    int GetToken(char *buffer, int maxLen);
    virtual int GetLine(char *buffer, int maxLen) override;
    float GetFloat();
    int GetInt();
    int MatchInt(const char* token) {
        MatchToken(token);
        return GetInt();
    }
    float MatchFloat(const char* token) {
        MatchToken(token);
        return GetFloat();
    }
    // Case-insensitive match + value (AGE 2.72).
    int MatchIInt(const char* token) {
        MatchIToken(token);
        return GetInt();
    }
    float MatchIFloat(const char* token) {
        MatchIToken(token);
        return GetFloat();
    }

    void GetVector(Vector2 &v);
    void GetVector(Vector3 &v);
    void GetVector(Vector4 &v);
    void SetTerminators(const char* terminators);
    void SetCommentChar(char c) { CommentChar = c; }
    bool IsTerminator(char c) const;
    void MatchVector(const char *token, Vector2 &v) { MatchToken(token); GetVector(v); }
    void MatchVector(const char *token, Vector3 &v) { MatchToken(token); GetVector(v); }
    void MatchVector(const char *token, Vector4 &v) { MatchToken(token); GetVector(v); }
    void MatchIVector(const char *token, Vector2 &v) { MatchIToken(token); GetVector(v); }
    void MatchIVector(const char *token, Vector3 &v) { MatchIToken(token); GetVector(v); }
    void MatchIVector(const char *token, Vector4 &v) { MatchIToken(token); GetVector(v); }

    void PutStr(const char *fmt, ...);
    void Put(const char *str);
    template<typename T, typename... Args>
    void Put(const char *fmt, T first, Args... args) {
        PutStr(fmt, first, args...);
    }
    void Put(int val) { PutStr("%d", val); }
    void Put(float val) { PutStr("%f", val); }
    void Put(const class Vector3 &v) { PutStr("%f %f %f", v.x, v.y, v.z); }
    void Put(const class Vector4 &v) { PutStr("%f %f %f %f", v.x, v.y, v.z, v.w); }
    void PutDelimiter(const char *str);

    void Indent(int delta) { m_Indent += delta; if (m_Indent < 0) m_Indent = 0; }
    void SetIndent(int level) { m_Indent = level < 0 ? 0 : level; }
    int GetIndent() const { return m_Indent; }
    void WriteTabs(int count);

    void StartBlock();
    void EndBlock();
    void StartLine();
    void EndLine();

    const char* GetCurrentToken() const { return m_CurrentToken; }
    void PrintFileContext();

    Stream* GetStream() const { return m_Stream; }
    const char* GetName() const { return filename; }

    int nextch;
    Stream *S;
    void SkipComment();
    void SkipToEndOfLine();

protected:
    void NextToken();
    virtual bool SkipCommentChars();
    void SkipPendingComments();
    // Materialize the first token on demand. The initial read is deferred out of
    // Init() so it observes the CommentChar the caller sets afterwards (e.g. the
    // FSM loader sets ';' only after Init()).
    void SyncWithStream();
    void EnsurePrimed();

    Stream *m_Stream;
    char *m_Buffer;
    int m_BufLen;
    int m_Cursor;
    char m_CurrentToken[256];
    bool m_HasToken;
    bool m_Primed;
    char m_Terminators[16];
    int m_Indent;
};

class datTokenizer : public datAsciiTokenizer {
public:
    datTokenizer() : datAsciiTokenizer() {}

    datTokenizer(const char *name, Stream *stream) : datAsciiTokenizer() { Init(name, stream); }
    virtual ~datTokenizer() {}
};

// Binary counterpart of datAsciiTokenizer (AGE 2.72).  Loaders and exporters
// are written against datBaseTokenizer and read/write fields in a FIXED order,
// so the same traversal drives both text and binary files.  Layout (all
// little-endian, no alignment):
//   token / Put(str) / PutStr : u16 byte length, then the bytes (no NUL)
//   Put(int) / GetInt         : s32
//   Put(float) / GetFloat     : f32
//   vectors                   : component floats in order
//   delimiters                : nothing (PutDelimiter/GetDelimiter are no-ops)
// MatchToken reads a string and verifies it; CheckToken peeks (rewinds when
// the token does not match or consume is false).
class datBinTokenizer : public datBaseTokenizer {
public:
    datBinTokenizer();
    datBinTokenizer(const char *name, Stream *stream);
    virtual ~datBinTokenizer();

    void Init(const char *name, Stream *stream);

    virtual int GetToken(char *buffer, int maxLen) override;
    virtual int GetLine(char *buffer, int maxLen) override;
    virtual bool CheckToken(const char *token, bool consume = true) override;
    virtual bool CheckIToken(const char *token, bool consume = true) override;
    virtual void MatchToken(const char *token) override;
    virtual void MatchIToken(const char *token) override;
    virtual int MatchInt(const char *token) override { MatchToken(token); return GetInt(); }
    virtual float MatchFloat(const char *token) override { MatchToken(token); return GetFloat(); }
    virtual int MatchIInt(const char *token) override { MatchIToken(token); return GetInt(); }
    virtual float MatchIFloat(const char *token) override { MatchIToken(token); return GetFloat(); }
    virtual void Pop() override { Quitf("datBinTokenizer::Pop - not implemented"); }
    virtual void GetDelimiter(const char *delim) override { Quitf("datBinTokenizer::GetDelimiter - not implemented"); }
    virtual float GetFloat() override;
    virtual int GetInt() override;
    virtual void GetVector(Vector3 &out) override;
    virtual void GetVector(Vector4 &out) override;
    void GetVector(Vector2 &out);
    virtual void MatchVector(const char *token, Vector2 &v) override { MatchToken(token); GetVector(v); }
    virtual void MatchVector(const char *token, Vector3 &v) override { MatchToken(token); GetVector(v); }
    virtual void MatchVector(const char *token, Vector4 &v) override { MatchToken(token); GetVector(v); }
    virtual void MatchIVector(const char *token, Vector3 &v) override { MatchToken(token); GetVector(v); }
    virtual void MatchIVector(const char *token, Vector4 &v) override { MatchToken(token); GetVector(v); }

    virtual void PutStr(const char *fmt, ...) override;
    virtual void Put(const char *str) override;
    virtual void Put(const Vector3 &v) override;
    virtual void Put(const Vector4 &v) override;
    void Put(const Vector2 &v);
    virtual void Put(int val) override;
    virtual void Put(float val) override;
    virtual void PutDelimiter(const char *str) override { Quitf("datBinTokenizer::PutDelimiter - not implemented"); }
    virtual bool SkipCommentChars() override { return false; }

    void StartBlock() { Quitf("datBinTokenizer::StartBlock - not implemented"); }
    void EndBlock() { Quitf("datBinTokenizer::EndBlock - not implemented"); }
    void StartLine() { Quitf("datBinTokenizer::StartLine - not implemented"); }
    void EndLine() { Quitf("datBinTokenizer::EndLine - not implemented"); }

    Stream* GetStream() const { return m_Stream; }
    const char* GetName() const { return filename; }
    bool AtEnd() const;

protected:
    bool ReadRaw(void *dst, int bytes);
    void WriteRaw(const void *src, int bytes);
    int ReadString(char *buffer, int maxLen);

    Stream *m_Stream;
    bool m_Eof;
};

#endif // DATA_TOKEN_H
