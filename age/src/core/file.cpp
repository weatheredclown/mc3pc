#include "core/file.h"

#include "core/stream.h"
#include "zipfile/zipfile.h"

#include <fcntl.h>
#include <io.h>
#include <mutex>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <direct.h>

// ---------------------------------------------------------------------------
// coreFileStandard: C runtime files.  Handles are the CRT descriptors.
// ---------------------------------------------------------------------------

static int sStdOpen(const char *pathname, bool readOnly)
{
	if (!pathname) return -1;
	int h = _open(pathname, readOnly ? (_O_RDONLY | _O_BINARY) : (_O_RDWR | _O_BINARY), _S_IREAD | _S_IWRITE);
	return h;
}

#include <direct.h>

static void sEnsureDir(const char *pathname)
{
	char temp[512];
	strncpy(temp, pathname, sizeof(temp) - 1);
	temp[sizeof(temp) - 1] = 0;
	for (char *p = temp; *p; p++) {
		if (*p == '/' || *p == '\\') {
			char orig = *p;
			*p = 0;
			if (temp[0] && !(temp[1] == ':' && temp[2] == 0)) {
				_mkdir(temp);
			}
			*p = orig;
		}
	}
}

static int sStdCreate(const char *pathname)
{
	if (!pathname) return -1;
	int h = _open(pathname, _O_RDWR | _O_CREAT | _O_TRUNC | _O_BINARY, _S_IREAD | _S_IWRITE);
	if (h < 0) {
		sEnsureDir(pathname);
		h = _open(pathname, _O_RDWR | _O_CREAT | _O_TRUNC | _O_BINARY, _S_IREAD | _S_IWRITE);
	}
	return h;
}

static int sStdRead(int handle, void *dest, int length)
{
	if (handle < 0) return -1;
	return _read(handle, dest, (unsigned)length);
}

static int sStdWrite(int handle, const void *src, int length)
{
	if (handle < 0) return -1;
	return _write(handle, src, (unsigned)length);
}

static int sStdSeek(int handle, int offset, seekWhence whence)
{
	if (handle < 0) return -1;
	int origin = whence == seekSet ? SEEK_SET : whence == seekCur ? SEEK_CUR : SEEK_END;
	return (int)_lseek(handle, offset, origin);
}

static int sStdClose(int handle)
{
	if (handle < 0) return -1;
	return _close(handle);
}

static int sStdSize(int handle)
{
	if (handle < 0) return -1;
	return (int)_filelength(handle);
}

static int sStdFlush(int handle)
{
	if (handle < 0) return -1;
	return _commit(handle);
}

coreFileMethods coreFileStandard = {
	sStdOpen, sStdCreate, sStdRead, sStdWrite, sStdSeek, sStdClose, 0, sStdSize, sStdFlush
};

// ---------------------------------------------------------------------------
// coreFileArchive: mounted .dat archives first, plain files second.
// Archive entries are Stream objects kept in a handle table; their handles
// live above ARCHIVE_BASE so they never collide with CRT descriptors.
// ---------------------------------------------------------------------------

enum { ARCHIVE_BASE = 0x40000000, ARCHIVE_SLOTS = 256 };

static Stream *s_ArchiveSlots[ARCHIVE_SLOTS];
static std::mutex s_ArchiveLock;

static Stream *sSlot(int handle)
{
	int idx = handle - ARCHIVE_BASE;
	if (idx < 0 || idx >= ARCHIVE_SLOTS) return 0;
	return s_ArchiveSlots[idx];
}

static int sArcOpen(const char *pathname, bool readOnly)
{
	if (!pathname) return -1;
	if (readOnly) {
		Stream *s = zipFile::OpenFromArchives(pathname);
		if (s) {
			std::lock_guard<std::mutex> lock(s_ArchiveLock);
			for (int i = 0; i < ARCHIVE_SLOTS; i++) {
				if (!s_ArchiveSlots[i]) {
					s_ArchiveSlots[i] = s;
					return ARCHIVE_BASE + i;
				}
			}
			s->Close();
			return -1;
		}
	}
	return sStdOpen(pathname, readOnly);
}

static int sArcRead(int handle, void *dest, int length)
{
	Stream *s = sSlot(handle);
	if (s) return s->Read(dest, length);
	return sStdRead(handle, dest, length);
}

static int sArcWrite(int handle, const void *src, int length)
{
	Stream *s = sSlot(handle);
	if (s) return s->Write(src, length);
	return sStdWrite(handle, src, length);
}

static int sArcSeek(int handle, int offset, seekWhence whence)
{
	Stream *s = sSlot(handle);
	if (s) {
		int pos = offset;
		if (whence == seekCur) pos = s->Tell() + offset;
		else if (whence == seekEnd) pos = s->Size() + offset;
		if (pos < 0) pos = 0;
		s->Seek(pos);
		return pos;
	}
	return sStdSeek(handle, offset, whence);
}

static int sArcClose(int handle)
{
	Stream *s = sSlot(handle);
	if (s) {
		{
			std::lock_guard<std::mutex> lock(s_ArchiveLock);
			s_ArchiveSlots[handle - ARCHIVE_BASE] = 0;
		}
		s->Close();
		return 0;
	}
	return sStdClose(handle);
}

static int sArcSize(int handle)
{
	Stream *s = sSlot(handle);
	if (s) return s->Size();
	return sStdSize(handle);
}

static int sArcFlush(int handle)
{
	Stream *s = sSlot(handle);
	if (s) { s->Flush(); return 0; }
	return sStdFlush(handle);
}

coreFileMethods coreFileArchive = {
	sArcOpen, sStdCreate, sArcRead, sArcWrite, sArcSeek, sArcClose, 0, sArcSize, sArcFlush
};

bool coreFileCreatePath(const char *pathname)
{
	if (!pathname || !*pathname)
		return false;

	char path[512];
	strncpy(path, pathname, sizeof(path) - 1);
	path[sizeof(path) - 1] = 0;

	// Walk the separators, creating each directory in turn.  Everything after
	// the last separator is the file name and is left alone; an already-present
	// directory is not an error.
	for (char *p = path; *p; p++)
	{
		if (*p != '/' && *p != '\\')
			continue;
		const char sep = *p;
		*p = 0;
		// Skip a leading "/" and a bare drive letter ("C:").
		if (path[0] && !(p == path) && !(p == path + 2 && path[1] == ':'))
			_mkdir(path);
		*p = sep;
	}
	return true;
}

int coreFileSizeViaSeek(const coreFileMethods *m, int handle)
{
	if (!m || !m->seek) return -1;
	int cur = m->seek(handle, 0, seekCur);
	int end = m->seek(handle, 0, seekEnd);
	m->seek(handle, cur, seekSet);
	return end;
}
