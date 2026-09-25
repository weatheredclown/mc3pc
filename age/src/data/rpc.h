#ifndef DATA_RPC_H
#define DATA_RPC_H

#include "core/output.h"
#include "core/types.h"

typedef int (*rpcHandlerFunc)(const char *cmd, void *dest, int destSize);

class rpcCommand {
public:
    rpcCommand(const char* name, rpcHandlerFunc handler) {}
};

inline void rpcCallf(const char* fmt, ...) { Quitf("rpcCallf - not implemented"); }
inline void rpcInit(const char* arg = nullptr) { Quitf("rpcInit - not implemented"); }
inline void rpcUpdate(int arg = 0) { Quitf("rpcUpdate - not implemented"); }

#endif // DATA_RPC_H
