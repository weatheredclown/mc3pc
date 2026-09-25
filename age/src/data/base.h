#include "core/output.h"
////////////////////////////////////////
// base.h
////////////////////////////////////////

#ifndef DATA_BASE_H
#define DATA_BASE_H

class Base {
public:
    virtual ~Base() {}
    static void InitClass(void) { Quitf("Base::InitClass - not implemented"); }
    static void ShutdownClass(void) { Quitf("Base::ShutdownClass - not implemented"); }
    virtual void Init(Base &parent, const Base &type) { Quitf("Base::Init - not implemented"); }
    virtual void SetEntityTypeManager_Hacked(class entTypeManager &mgr) { Quitf("Base::SetEntityTypeManager_Hacked - not implemented"); }
};

#endif // DATA_BASE_H
