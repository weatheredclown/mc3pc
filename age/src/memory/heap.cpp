#include "memory/heap.h"

#include "core/output.h"
#include "core/stream.h"
#include "data/assetcfg.h"
#include "data/args.h"
#include "memory/allocator.h"
#include "zipfile/zipfile.h"

#include <stdio.h>
#include <string.h>

memMemoryAllocator *memHeap::sm_Allocator = 0;
char memHeap::sm_AssetPath[256] = "";
bool memHeap::sm_ArchiveMounted = false;

void (*memHeap::sm_InitAppHook)(bool setRes) = 0;

void memHeap::InitClass(const char *assetPath, int megabytes, bool setRes, bool /*ignorePrefix*/, bool /*addExt*/)
{
	if (assetPath) {
		strncpy(sm_AssetPath, assetPath, sizeof(sm_AssetPath) - 1);
		sm_AssetPath[sizeof(sm_AssetPath) - 1] = '\0';
	}

	if (!sm_Allocator) {
		sm_Allocator = new memMemoryAllocator;
		sm_Allocator->Init(0, (unsigned)megabytes * 1024u * 1024u, false);
	}
	memMemoryAllocator::SetCurrent(sm_Allocator);

	// The game already called ASSET.SetPath(); make sure the root is set when it
	// did not (tools call InitClass alone).
	if (assetPath && assetPath[0] && !ASSET.GetPath()[0]) {
		ASSET.SetPath(assetPath);
	}

	// the application half of InitClass: graphics pipeline up now
	if (sm_InitAppHook)
		sm_InitAppHook(setRes);
}

void memHeap::ShutdownClass()
{
	End();
	if (sm_Allocator) {
		sm_Allocator->Kill();
		delete sm_Allocator;
		sm_Allocator = 0;
	}
}

// Mount one archive, resolving a bare name against the asset root.  Returns
// false when the file is not there (the caller then falls back to loose files).
static bool sMountOne(const char *assetPath, const char *archive)
{
	char path[512];
	if (assetPath[0] && archive[0] != '/' && archive[0] != '\\' && archive[1] != ':') {
		snprintf(path, sizeof(path), "%s%s%s", assetPath,
			(assetPath[strlen(assetPath) - 1] == '/' || assetPath[strlen(assetPath) - 1] == '\\') ? "" : "/",
			archive);
	} else {
		strncpy(path, archive, sizeof(path) - 1);
		path[sizeof(path) - 1] = '\0';
	}

	zipFile *z = new zipFile;
	if (z->Init(path)) {
		Displayf("memHeap::Begin - mounted '%s'", path);
		return true;
	}

	delete z;
	Displayf("memHeap::Begin - no archive '%s'", path);
	return false;
}

void memHeap::Begin(const char *archive)
{
	// -archive <list> replaces the compiled-in archive name, so a build can be
	// pointed at the shipped .dat set (e.g. "-path ../assets -archive
	// ASSETS.DAT;TEXTURE.DAT") instead of a loose asset tree.  Separate several
	// archives with ';' or ','; they are searched in the order given.
	const char *list = archive;
	const char *argArchive = 0;
	if (ARGS.Get("archive", 0, &argArchive) && argArchive && argArchive[0])
		list = argArchive;

	if (!list || !list[0]) return;

	while (*list) {
		while (*list == ';' || *list == ',' || *list == ' ') list++;
		const char *end = list;
		while (*end && *end != ';' && *end != ',') end++;

		size_t len = (size_t)(end - list);
		while (len && (list[len - 1] == ' ')) len--;
		if (len && len < 256) {
			char name[256];
			memcpy(name, list, len);
			name[len] = '\0';
			if (sMountOne(sm_AssetPath, name))
				sm_ArchiveMounted = true;
		}
		list = end;
		if (*list) list++;
	}

	if (!sm_ArchiveMounted)
		Displayf("memHeap::Begin - no archive mounted; loading loose assets from '%s'", sm_AssetPath);
}

void memHeap::End()
{
	if (sm_ArchiveMounted) {
		zipFile::KillAll();
		sm_ArchiveMounted = false;
	}
}
