#ifndef NET_ENGINE_NETENGINEPROTOCOL_H
#define NET_ENGINE_NETENGINEPROTOCOL_H

////////////////////////////////////////
// net_engine/netengineprotocol.h
//
// netEngineProtocol - names the commands and phases for logging and
// carries the protocol version peers must agree on.  Game commands start
// at kGAME_COMMAND_0; everything below is the engine's.
////////////////////////////////////////

#include "core/types.h"
#include "net_engine/netengineconsts.h"

enum {
    kGAME_COMMAND_0 = 100
};

class netEngineProtocol {
public:
    netEngineProtocol() {}
    virtual ~netEngineProtocol() {}

    virtual u32 GetVersion() const { return 1; }
    virtual const char *CommandToString(int command);
    virtual const char *PhaseToString(int phase);
};

#endif // NET_ENGINE_NETENGINEPROTOCOL_H
