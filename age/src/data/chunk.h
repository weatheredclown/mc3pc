#ifndef DATA_CHUNK_H
#define DATA_CHUNK_H

////////////////////////////////////////
// data/chunk.h
//
// datChunk - the game's entry point for resource packs.  Load<T> opens
// "<name>.pck" as a datResourceImage and asks datResourceBuilder<T> to
// construct the port's real T from the image (see data/rscimage.h); the
// pack is never paged in as live objects.  The build/stream side (the
// console tools that wrote the packs) stays unimplemented on PC.
////////////////////////////////////////

#include "core/types.h"
#include "core/output.h"
#include "data/rscimage.h"
#include "data/pager.h"
#include "data/assetcfg.h"

class datChunk {
public:
	struct StreamInfo {
		int handle;
		void *data;
		int size;
		void *Sema;      // completion semaphore of an async stream (NULL = none pending)
		char path[256];
		StreamInfo() : handle(0), data(0), size(0), Sema(0) { path[0] = 0; }
	};

	// Loads the pack and builds `ptr`; false (ptr NULL) when the pack is
	// missing, its version does not match, or no builder exists for T.
	template <typename T>
	static bool Load(T *&ptr, const char *filename, int version = 0) {
		ptr = 0;
		datResourceImage image;
		if (!image.Load(filename))
			return false;
		if (version != 0 && (int)image.GetVersion() != version)
			Warningf("datChunk::Load: '%s.pck' is version %u, the code expects %d - reading it anyway", filename, image.GetVersion(), version);
		if (const char *outDir = datResourceUnpackDir())
			datResourceUnpacker<T>::Unpack(image, outDir);      // -unpackresources <dir>: write loose files too
		ptr = datResourceBuilder<T>::Build(image);
		return ptr != 0;
	}

	template <typename T>
	static bool Free(T *ptr) {
		delete ptr;
		return true;
	}

	static void BeginBuild(int size) { Quitf("datChunk::BeginBuild - not implemented"); }
	static void SaveBuild(const char *filename, int version = 0) { Quitf("datChunk::SaveBuild - not implemented"); }
	static void EndBuild() { Quitf("datChunk::EndBuild - not implemented"); }
	static void DisableDelete() { Quitf("datChunk::DisableDelete - not implemented"); }
	static void EnableDelete() { Quitf("datChunk::EnableDelete - not implemented"); }
	static void PushLowerPageSize(int size) { Quitf("datChunk::PushLowerPageSize - not implemented"); }

	static bool BeginStream(const char *path, StreamInfo &info) {
		if (!path) return false;
		const char *p = path;
		const char *root = ASSET.GetPath();
		if (root && root[0]) {
			int rlen = (int)strlen(root);
			if (strncmp(p, root, rlen) == 0) {
				p += rlen;
				while (*p == '/' || *p == '\\') p++;
			}
		}
		const char *chnk = strstr(p, ".chnk");
		int len = chnk ? (int)(chnk - p) : (int)strlen(p);
		if (len >= (int)sizeof(info.path)) len = (int)sizeof(info.path) - 1;
		int i = 0;
		for (; i < len; ++i) info.path[i] = p[i];
		info.path[i] = 0;
		return true;
	}

	template <typename T>
	static bool EndStream(T *&ptr, StreamInfo &info, bool flag = false) {
		(void)flag;
		if (info.path[0] == 0) return false;
		return Load<T>(ptr, info.path);
	}
};

class datChunkRefBase {
public:
	static void InitPageFile(const char *filename, int index, int count, int pageSize) { Quitf("datChunkRefBase::InitPageFile - not implemented"); }
	static void ShutdownPageFile() { Quitf("datChunkRefBase::ShutdownPageFile - not implemented"); }
	static void BeginCapture(int id) { Quitf("datChunkRefBase::BeginCapture - not implemented"); }
	template <typename T>
	static void EndCapture(T *ptr, int objNum, const char *name1, const char *name2) { Quitf("datChunkRefBase::EndCapture - not implemented"); }
};

extern const char *datChunkExt;

#endif // DATA_CHUNK_H
