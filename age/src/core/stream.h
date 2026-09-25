////////////////////////////////////////
// stream.h
////////////////////////////////////////

#ifndef CORE_STREAM_H
#define CORE_STREAM_H

#include "core/output.h"
#include "core/types.h"
#include <stdio.h>
#include <stdarg.h>
#include "core/file.h"

// <windows.h> #defines SetCurrentDirectory -> SetCurrentDirectoryA.  types.h no
// longer force-includes windows.h, so replicate that one rename here to keep this
// method's name matching call sites that DO see windows.h (e.g. rb testanim3.cpp).
#if defined(_WIN32) && !defined(SetCurrentDirectory)
#define SetCurrentDirectory SetCurrentDirectoryA
#endif

struct coreFileMethods;

class Stream {
public:
    virtual ~Stream() {}
    virtual void Close() = 0;
    virtual const char* GetName() const { return ""; }
    virtual int Read(void *pData, int nSize) = 0;
    virtual int Write(const void *pData, int nSize) { return 0; }
    virtual void Flush() { Quitf("Stream::Flush - not implemented"); }
    static void SetAssetRoot(const char* path) { Quitf("Stream::SetAssetRoot - not implemented"); }
    static void SetCurrentDirectory(const char* path) { Quitf("Stream::SetCurrentDirectory - not implemented"); }

    // Open/Create go through a coreFileMethods table (core/file.h).  The
    // default table is the archive-aware one; the game may replace or wrap it.
    static Stream* Create(const char* filename);
    static Stream* Create(const char* filename, const coreFileMethods *methods);
    static Stream* Open(const char* filename);
    static Stream* Open(const char* filename, const coreFileMethods *methods);
    static const coreFileMethods* GetDefaultFileOpenMethods();
    static void SetDefaultFileOpenMethods(const coreFileMethods *methods);

    static Stream* PreLoad(Stream* s) { return s; }
    static void DumpOpenFiles() { Quitf("Stream::DumpOpenFiles - not implemented"); }

    virtual int GetCh() { return -1; }
    virtual int FastGetCh() { return -1; }
    virtual int Tell() { return 0; }
    virtual void Seek(int offset) { Quitf("Stream::Seek - not implemented"); }
    virtual int Size() { return 0; }
    virtual int FastPutCh(int c) { return -1; }
    virtual void PreLoad(int size) { Quitf("Stream::PreLoad - not implemented"); }

    // Typed convenience I/O.  `count` = number of elements.
    void ReadShort(void *pData, int count)        { Read(pData, count * 2); }
    void ReadInt(void *pData, int count)          { Read(pData, count * 4); }
    void ReadFloat(void *pData, int count)        { Read(pData, count * 4); }
    void WriteInt(const void *pData, int count)   { Write(pData, count * 4); }
    void WriteFloat(const void *pData, int count) { Write(pData, count * 4); }
    void WriteShort(const void *pData, int count) { Write(pData, count * 2); }
};

class SafeStream {
public:
    SafeStream() : m_Stream(NULL) {}
    SafeStream(Stream* pStream) : m_Stream(pStream) {}
    ~SafeStream() {
        if (m_Stream) m_Stream->Close();
    }
    SafeStream& operator=(Stream* pStream) {
        if (m_Stream) m_Stream->Close();
        m_Stream = pStream;
        return *this;
    }
    Stream* operator->() { return m_Stream; }
    const Stream* operator->() const { return m_Stream; }
    operator Stream*() { return m_Stream; }
    operator const Stream*() const { return m_Stream; }
    bool IsValid() const { return m_Stream != NULL; }
    operator bool() const { return m_Stream != NULL; }
private:
    Stream* m_Stream;
};

inline int fprintf(Stream *pStream, const char *fmt, ...) {
    if (!pStream) return 0;
    char buf[4096];
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (written > 0) {
        pStream->Write(buf, written);
    }
    return written;
}

inline int fprintf(const Stream *pStream, const char *fmt, ...) {
    if (!pStream) return 0;
    char buf[4096];
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (written > 0) {
        const_cast<Stream*>(pStream)->Write(buf, written);
    }
    return written;
}


// stdio-style line read from an AGE Stream (editor tooling: graph combo
// lists).  Reads up to and including the newline; returns 0 at end of file.
inline char *fgets(char *buf, int n, class Stream *s) {
    if (!s || n <= 0) return 0;
    int i = 0;
    char c;
    while (i < n - 1 && s->Read(&c, 1) == 1) {
        buf[i++] = c;
        if (c == '\n') break;
    }
    buf[i] = 0;
    return i > 0 ? buf : 0;
}

// The streaming handle pool (implementation in data/stream.cpp).  The console
// build backs this with a background DVD reader; this port reads synchronously,
// so the pool is a size and a live flag rather than a worker thread.
class datStreamer {
public:
    static void InitClass(int n = 4);
    static void ShutdownClass();
    static int GetNumHandles();

private:
    static int sm_NumHandles;
};

#endif // CORE_STREAM_H
