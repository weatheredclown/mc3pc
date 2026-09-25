////////////////////////////////////////
// main.h
////////////////////////////////////////

#ifndef DATA_MAIN_H
#define DATA_MAIN_H

#include "core/output.h"
#include "core/types.h"
#include "data/args.h"
#include "data/assetcfg.h"		// exposes the ASSET manager (datAssetManager) to app code

#define MKRGB(r,g,b) (((u32)(r) << 16) | ((u32)(g) << 8) | (u32)(b))

extern u32 ageClearColor;

//// Initialization & Shutdown functions ////
void ageInit(const char *path, bool arg2 = false, bool arg3 = false);
void ageBeginGfx(int width = 640, int height = 448, int arg3 = 32, int arg4 = 24, bool windowed = false);
void ageEndGfx();

inline void ageInitAge(const char *path = 0, bool arg2 = false, bool arg3 = false) {
	ageInit(path, arg2, arg3);
}

void ageInitBank(const char *name = 0);

//// Frame control functions ////
void ageBeginFrame();
inline void ageBeginFrame(bool clear) { (void)clear; ageBeginFrame(); }
void ageEndFrame();
bool ageExit();

#ifdef ACTOR_MANAGER_H
typedef aActorManagerBase<aActor, aGuid> aActorManager;
#endif



#ifdef ID
#undef ID
#endif
#define ID(className) aGlue<className>::smClassID

#if defined(COMPILING_TESTACTOR) && defined(ACTOR_COMPONENT_H)
class aComponent_Hack : public aComponent {
public:
    void Init(aActor &parent, const aComponentType &type) { Quitf("aComponent_Hack::Init - not implemented"); }
};
#define aComponent aComponent_Hack

// actor.h's CALL macro expands to GetParent().GetComponentFromIndex(...).
// GetParent() is a member of aComponent, so CALL works inside component methods
// (as HANDLE_MESSAGE does). But testactor's Main() invokes CALL from
// free-function scope, where there is no member GetParent(); the macro comment
// notes it "assumes there is a variable aActor &parent" — Main() holds
// `aActor &parent = actor1B.GetActor()` for "Actor1". Supply a free GetParent()
// for the free-scope expansion to bind to. Inside component methods the member
// still wins by name lookup, so HANDLE_MESSAGE is unaffected.
inline aActor &GetParent(void) { return aGuid("Actor1").GetActor(); }
#endif

class aExportHeader {
public:
    static void ExportTo(const char* name) { Quitf("aExportHeader::ExportTo - not implemented"); }
    static void Close() { Quitf("aExportHeader::Close - not implemented"); }
};

#define NUM_aComponentA 3
#define INDEX_aComponentA 0

#define NUM_aComponentB 4
#define INDEX_aComponentB 3

#define NUM_aComponentC 5
#define INDEX_aComponentC 7

#endif // DATA_MAIN_H
