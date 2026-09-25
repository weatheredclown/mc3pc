#ifndef MEMORY_HEAP_H
#define MEMORY_HEAP_H

////////////////////////////////////////
// memory/heap.h
//
// memHeap - the game's main heap plus the asset archive it is paired with.
// InitClass sizes the heap and sets the asset root, Begin(archive) mounts the
// .dat archive the game loads from, End/ShutdownClass tear it back down.
////////////////////////////////////////

class memMemoryAllocator;

class memHeap
{
public:
	static void InitClass(const char *assetPath, int megabytes, bool setRes = true, bool ignorePrefix = false, bool addExt = false);
	// AGE application init: InitClass also brings up the graphics pipeline
	// (window + device at the resolution set beforehand with PIPE.SetRes).
	// The gfx library registers the hook so memory/ stays independent of it.
	static void (*sm_InitAppHook)(bool setRes);
	static void ShutdownClass();

	static void Begin(const char *archive);
	static void End();

	static memMemoryAllocator *GetAllocator()	{ return sm_Allocator; }
	static const char *GetAssetPath()			{ return sm_AssetPath; }
	static bool IsArchiveMounted()				{ return sm_ArchiveMounted; }

private:
	static memMemoryAllocator *sm_Allocator;
	static char sm_AssetPath[256];
	static bool sm_ArchiveMounted;
};

#endif // MEMORY_HEAP_H
