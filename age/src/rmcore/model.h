#ifndef RMCORE_MODEL_H
#define RMCORE_MODEL_H

////////////////////////////////////////
// rmcore/model.h
//
// rmcModel lives in rmcore/drawable.h (an rmcDrawable loaded from a .type
// file); this header adds the model factory the game swaps in to control
// where models come from.
////////////////////////////////////////

#include "rmcore/drawable.h"

class rmcModelFactory {
public:
    static rmcModelFactory *sm_Instance;
    static rmcModelFactory &GetInstance() { return *sm_Instance; }

    rmcModelFactory() {}
    virtual ~rmcModelFactory() {}

    static rmcModelFactory *CreateStandardModelFactory() { return new rmcModelFactory(); }
    static void PushInstance(rmcModelFactory *inst = nullptr);
    static void PushInstance(rmcModelFactory &inst);
    static void PopInstance();
    virtual rmcModel *Create(const char *name, int mtxIndex = 0) { (void)mtxIndex; return rmcModel::Create(name); }
};

#endif // RMCORE_MODEL_H
