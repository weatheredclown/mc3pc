#ifndef NET_ENGINE_NETENGINECONSTS_H
#define NET_ENGINE_NETENGINECONSTS_H

////////////////////////////////////////
// net_engine/netengineconsts.h
//
// Shared networking vocabulary: the `net` namespace constants the game
// spells (quit reasons, session phases, name/password lengths, message
// priorities, ping averaging, NAT states, join flags) plus netAddress and
// netGUID.  netHardware lives in net2/nethardware.h.
////////////////////////////////////////

#ifndef __GAMESPY
#define __GAMESPY 1
#endif

#include "core/types.h"
#include "data/unicode.h"
#include <string.h>

namespace net {
    enum eQuitReason {
        kQuitReasonUserQuit = 0,
        kQuitReasonHostQuit,
        kQuitReasonKicked,
        kQuitReasonConnectionLost,
        kQuitReasonError,
        kQuitReasonUnknown,
        // 2.72 spellings
        kUnknown = kQuitReasonUnknown,
        kPeerQuit = kQuitReasonUserQuit,
        kHostQuit = kQuitReasonHostQuit,
        kEjected = kQuitReasonKicked,
        kBadConnection = kQuitReasonConnectionLost,
        kJoinFailed = 100,
        kSessionSealed,
        kSessionFull,
        kNotHosting,
        kBadPassword,
        kInvalidPacket,
        kBadVersion,
        kTimedOut
    };
    const char *QuitReasonToString(eQuitReason reason);

    enum ePhase {
        PHASE_NONE = 0,
        PHASE_CONNECTING,
        PHASE_STAGING,
        PHASE_LOADING_FRONTEND,
        PHASE_LOADING_GAME,
        PHASE_PLAYING,
        PHASE_POSTGAME
    };
    const char *PhaseToString(ePhase phase);

    enum { kMaxNameLength = 32, kMaxPasswordLength = 32 };

    enum ePriority { kPriorityUnreliable = 0, kPriorityReliable = 1 };

    // Ping statistics: running averages over these horizons (ms).
    enum { kNumAverages = 3 };
    extern const float kAverageTimeHorizons[kNumAverages];

    enum eNatState {
        kNatUninitialized = 0,
        kNatSuccess,
        kNatErrorUnknown,
        kNatErrorPingTimeout,
        kNatErrorInitTimeout,
        kNatErrorDeadbeatPartner
    };

    // netHardware::GetError() values (mirrored in netHardware::eError).
    enum eHardwareError {
        kErrorNone = 0,
        kErrorUnknown,
        kErrorBadMagic,
        kErrorUnsupportedDevice,
        kErrorNoHardware,
        kErrorNetworkUnavailable
    };

    enum eNetworkType { eNetworkTypeLAN = 0, eNetworkTypeInternet = 1 };

    enum { kDefaultPort = 6500, kNumPorts = 16 };
}

// Debug channel level for the networking Debugf calls (0 = quiet).
extern int netDebug;

class bkBank;

// Byte-order helpers (host <-> network) without pulling in winsock.
inline u32 age_htonl(u32 v) { return (v >> 24) | ((v >> 8) & 0xff00u) | ((v << 8) & 0xff0000u) | (v << 24); }
inline u32 age_ntohl(u32 v) { return age_htonl(v); }
inline u16 age_htons(u16 v) { return (u16)((v >> 8) | (v << 8)); }
inline u16 age_ntohs(u16 v) { return age_htons(v); }

class netAddress {
public:
    u32 ip;     // host byte order
    u16 port;   // host byte order

    netAddress() : ip(0), port(0) {}
    netAddress(u32 _ip, u16 _port) : ip(_ip), port(_port) {}
    void Set(u32 _ip, u16 _port) { ip = _ip; port = _port; }
    void Reset() { ip = 0; port = 0; }
    void Clear() { Reset(); }
    u32 GetIP() const { return ip; }
    u32 GetAddress() const { return ip; }
    u16 GetPort() const { return port; }
    void SetAddress(u32 _ip) { ip = _ip; }
    void SetPort(u16 _port) { port = _port; }
    bool IsValid() const { return ip != 0 && port != 0; }
    bool operator==(const netAddress &other) const { return ip == other.ip && port == other.port; }
    bool operator!=(const netAddress &other) const { return !(*this == other); }
    // "a.b.c.d:port" into `buf` (at least 24 chars); returns buf.
    char *Format(char *buf) const;
    const char *ToString(char *buf, int bufLen) const;
    // Parse "a.b.c.d[:port]" (port defaults to net::kDefaultPort).
    bool FromString(const char *text);
};

class netGUID {
public:
    u64 data;

    static netGUID sm_Dummy;

    netGUID() : data(0) {}
    netGUID(u64 val) : data(val) {}
    netGUID(int val) : data(val < 0 ? 0 : (u64)val) {}
    netGUID(u32 hi, u32 lo) : data(((u64)hi << 32) | lo) {}
    // Generate a new random id when `generateNew`, else clear.
    void Reset(bool generateNew = false);
    bool IsValid() const { return data != 0; }
    bool IsDummy() const { return data == 0; }
    u64 GetData() const { return data; }
    // Profile id for services keyed by a 32-bit id; -1 when unset.
    int GetProfileID() const { return data ? (int)(data & 0x7fffffffu) : -1; }
    bool operator==(const netGUID &other) const { return data == other.data; }
    bool operator!=(const netGUID &other) const { return data != other.data; }
    bool operator<(const netGUID &other) const { return data < other.data; }
};

#endif // NET_ENGINE_NETENGINECONSTS_H
