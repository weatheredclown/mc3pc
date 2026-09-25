#ifndef CRFANIMATION_FX_H
#define CRFANIMATION_FX_H

////////////////////////////////////////
// crfanimation/fx.h
//
// crfAnimFXData - per-animation effect data (.animfx sidecar), created via
// a game-installed factory so the game can subclass it.
////////////////////////////////////////

#include "core/types.h"

class datTokenizer;

typedef class crfAnimFXData *(*crfAnimFXDataCreateInstanceFunc)();

class crfAnimFXData {
public:
	crfAnimFXData() {}
	virtual ~crfAnimFXData() {}

	virtual void Load(datTokenizer &tok);

	static void SetCreateFunction(crfAnimFXDataCreateInstanceFunc func) { sm_CreateFunction = func; }
	static crfAnimFXData *CreateInstance() { return sm_CreateFunction ? sm_CreateFunction() : NULL; }

private:
	static crfAnimFXDataCreateInstanceFunc sm_CreateFunction;
};

#endif // CRFANIMATION_FX_H
