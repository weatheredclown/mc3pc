////////////////////////////////////////
// stream.cpp
//
// Stream::Open/Create over a coreFileMethods table (core/file.h).  The
// default table is coreFileArchive (mounted .dat archives, then plain files);
// the game may install a wrapper table to observe or redirect I/O.
////////////////////////////////////////

#include "core/stream.h"
#include "core/file.h"
#include <stdio.h>
#include <string.h>

static const coreFileMethods *s_DefaultMethods = &coreFileArchive;

// A Stream bound to (methods, handle) with a small read buffer so the
// per-character tokenizer paths do not pay a call per byte.
class MethodStream : public Stream {
public:
    MethodStream(const coreFileMethods *methods, int handle, const char *name)
        : m_Methods(methods), m_Handle(handle), m_Pos(0), m_BufStart(0), m_BufLen(0), m_BufPos(0) {
        strncpy(m_Name, name ? name : "", sizeof(m_Name) - 1);
        m_Name[sizeof(m_Name) - 1] = '\0';
    }
    virtual ~MethodStream() {
        if (m_Handle >= 0 && m_Methods->close) m_Methods->close(m_Handle);
    }
    virtual const char* GetName() const override { return m_Name; }
    virtual void Close() override { delete this; }

    virtual int Read(void *pData, int nSize) override {
        if (nSize <= 0) return 0;
        char *dst = (char *)pData;
        int total = 0;
        // drain the buffer first
        int avail = m_BufLen - m_BufPos;
        if (avail > 0) {
            int n = avail < nSize ? avail : nSize;
            memcpy(dst, m_Buf + m_BufPos, n);
            m_BufPos += n; dst += n; nSize -= n; total += n; m_Pos += n;
        }
        if (nSize > 0) {
            DropBuffer();
            int n = m_Methods->read(m_Handle, dst, nSize);
            if (n > 0) { total += n; m_Pos += n; }
        }
        return total;
    }
    virtual int Write(const void *pData, int nSize) override {
        DropBuffer();
        if (!m_Methods->write) return 0;
        int n = m_Methods->write(m_Handle, pData, nSize);
        if (n > 0) m_Pos += n;
        return n;
    }
    virtual void Flush() override {
        if (m_Methods->flush) m_Methods->flush(m_Handle);
    }
    virtual int GetCh() override {
        if (m_BufPos >= m_BufLen) {
            if (!Fill()) return -1;
        }
        m_Pos++;
        return (unsigned char)m_Buf[m_BufPos++];
    }
    virtual int FastGetCh() override { return GetCh(); }
    virtual int Tell() override { return m_Pos; }
    virtual void Seek(int offset) override {
        // seek inside the buffer when possible.  An empty buffer never
        // qualifies: after a direct (unbuffered) Read m_BufStart still names
        // the position the read started at while the handle sits past it, so
        // a Seek back to that start must reach the file (page files re-read a
        // page header right after reading the page).
        int bufOffset = offset - m_BufStart;
        if (m_BufLen > 0 && bufOffset >= 0 && bufOffset <= m_BufLen) {
            m_BufPos = bufOffset;
            m_Pos = offset;
            return;
        }
        DropBuffer();
        m_Methods->seek(m_Handle, offset, seekSet);
        m_Pos = offset;
    }
    virtual int Size() override {
        if (m_Methods->size) return m_Methods->size(m_Handle);
        // seek-based fallback keeps the logical position
        DropBuffer();
        int size = coreFileSizeViaSeek(m_Methods, m_Handle);
        m_Methods->seek(m_Handle, m_Pos, seekSet);
        return size;
    }
    virtual int FastPutCh(int c) override {
        char ch = (char)c;
        return Write(&ch, 1) == 1 ? c : -1;
    }
    virtual void PreLoad(int) override {}

private:
    enum { BUF_SIZE = 4096 };

    bool Fill() {
        // the physical position is m_BufStart + m_BufLen; resync when the
        // buffer is stale (after a seek outside it)
        int physical = m_BufStart + m_BufLen;
        if (physical != m_Pos) {
            m_Methods->seek(m_Handle, m_Pos, seekSet);
        }
        m_BufStart = m_Pos;
        m_BufPos = 0;
        int n = m_Methods->read(m_Handle, m_Buf, BUF_SIZE);
        m_BufLen = n > 0 ? n : 0;
        return m_BufLen > 0;
    }
    void DropBuffer() {
        if (m_BufLen) {
            // unread buffered bytes: rewind the physical position to m_Pos
            int physical = m_BufStart + m_BufLen;
            if (physical != m_Pos) m_Methods->seek(m_Handle, m_Pos, seekSet);
        }
        m_BufStart = m_Pos;
        m_BufLen = 0;
        m_BufPos = 0;
    }

    const coreFileMethods *m_Methods;
    int m_Handle;
    int m_Pos;
    int m_BufStart, m_BufLen, m_BufPos;
    char m_Buf[BUF_SIZE];
    char m_Name[256];
};

const coreFileMethods* Stream::GetDefaultFileOpenMethods() {
    return s_DefaultMethods;
}

void Stream::SetDefaultFileOpenMethods(const coreFileMethods *methods) {
    s_DefaultMethods = methods ? methods : &coreFileArchive;
}

Stream* Stream::Create(const char* filename, const coreFileMethods *methods) {
    if (!methods) methods = s_DefaultMethods;
    if (!filename || !methods->create) return NULL;
    int h = methods->create(filename);
    if (h < 0) return NULL;
    return new MethodStream(methods, h, filename);
}

Stream* Stream::Create(const char* filename) {
    return Create(filename, s_DefaultMethods);
}

int streamDebug = 0; // TODO: add Debugf1 output for stream operations so that, in a pinch, they could be debugged easily without a recompile

Stream* Stream::Open(const char* filename, const coreFileMethods *methods) {
    if (!methods) methods = s_DefaultMethods;
    if (!filename || !methods->open) return NULL;
    int h = methods->open(filename, true);
    if (h < 0) return NULL;
    return new MethodStream(methods, h, filename);
}

Stream* Stream::Open(const char* filename) {
    return Open(filename, s_DefaultMethods);
}

////////////////////////////////////////
// datStreamer
//
// The streaming handle pool.  On the console this owns the background DVD
// reader and hands out `n` concurrent asynchronous reads; the port's streams
// are synchronous (MethodStream reads on the calling thread, and every
// PreLoad override is a no-op), so there is no reader thread to start.
// InitClass sizes the pool and marks the class live, which is what the one
// caller - mc.cpp's startup - asks of it.
////////////////////////////////////////

int datStreamer::sm_NumHandles = 0;

void datStreamer::InitClass(int n)
{
	if (n < 1) n = 1;
	if (sm_NumHandles == n) return;			// already sized; startup may run twice
	sm_NumHandles = n;
	Displayf("datStreamer::InitClass: %d streaming handles (reads on this port are synchronous)", n);
}

void datStreamer::ShutdownClass()
{
	sm_NumHandles = 0;
}

int datStreamer::GetNumHandles()
{
	return sm_NumHandles;
}
