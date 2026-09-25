#ifndef CORE_FILE_H
#define CORE_FILE_H

////////////////////////////////////////
// core/file.h
//
// coreFileMethods - the pluggable low-level file layer under Stream.  A
// method table maps a path name to an integer handle and services reads,
// writes and seeks on it.  Stream::Open/Create build a Stream on top of a
// table; the game swaps or wraps tables to redirect I/O (memory cards, load
// progress tracking, the asset archive).
//
//   coreFileStandard - plain files through the C runtime.
//   coreFileArchive  - the port default: mounted .dat archives first, then
//                      plain files.
////////////////////////////////////////

enum seekWhence {
	seekSet = 0,
	seekCur = 1,
	seekEnd = 2
};

typedef void (*coreFileEnumCallback)(const char *filename, bool isDirectory, void *userData);

struct coreFileMethods
{
	int (*open)(const char *pathname, bool readOnly);          // handle, or -1
	int (*create)(const char *pathname);                        // handle, or -1
	int (*read)(int handle, void *dest, int length);            // bytes read, or -1
	int (*write)(int handle, const void *src, int length);      // bytes written, or -1
	int (*seek)(int handle, int offset, seekWhence whence);     // new position, or -1
	int (*close)(int handle);                                   // 0 on success
	int (*enumFiles)(const char *path, coreFileEnumCallback cb, void *userData);   // may be 0
	int (*size)(int handle);                                    // may be 0 (seek-based fallback)
	int (*flush)(int handle);                                   // may be 0
};

extern coreFileMethods coreFileStandard;
extern coreFileMethods coreFileArchive;

// Common helpers for method tables that lack the optional entries.
int coreFileSizeViaSeek(const coreFileMethods *m, int handle);

// Create the directories leading to `pathname`, which names a FILE - the last
// component is not created.  `create` opens a file but will not make the folder
// it lives in, so anything that writes assets back out into a tree that does not
// exist yet has to call this first.  True when the path is ready to be created
// in (including when it already existed).
bool coreFileCreatePath(const char *pathname);

#endif // CORE_FILE_H
