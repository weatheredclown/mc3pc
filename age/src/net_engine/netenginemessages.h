#ifndef NET_ENGINE_NETENGINEMESSAGES_H
#define NET_ENGINE_NETENGINEMESSAGES_H

////////////////////////////////////////
// net_engine/netenginemessages.h
//
// Engine-level messages (commands below kGAME_COMMAND_0): host discovery,
// the join handshake, quit notices, pings, phase changes; plus the
// thread -> manager event messages (reset, peer added/removed, phase
// changed) that the manager turns into Post* callbacks.  Games derive from
// the four handshake messages to append their own fields and hand them
// out through their netEngineFactory.
////////////////////////////////////////

#include "net2/netmessages.h"
#include "net_engine/netengineconsts.h"

enum netEngineCommand {
    kNET_QUERY_HOSTS = 1,
    kNET_HOST_INFO,
    kNET_JOIN_REQUEST,
    kNET_WELCOME,
    kNET_NEW_JOIN,
    kNET_QUIT,
    kNET_PING,
    kNET_PONG,
    kNET_PHASE,             // peer -> host: phase request
    kNET_SESSION_PHASE,     // host -> all: session phase changed
    kNET_PEER_PHASE,        // peer -> all: my phase changed
    kNET_KEEPALIVE,
    kNET_MIGRATED,          // peer -> new host: I know you're the host now

    // thread -> manager events (never sent on the wire)
    kNET_EVT_RESET = 40,
    kNET_EVT_PEER_ADDED,
    kNET_EVT_PEER_REMOVED,
    kNET_EVT_PHASE_CHANGED,

    kNET_LAST_ENGINE_COMMAND = 99
};

// Broadcast by a client looking for sessions.
class netQueryHostsMessage : public netMessage {
public:
    netQueryHostsMessage() : netMessage(kNET_QUERY_HOSTS), m_ProtocolVersion(0) { m_Length = sizeof(*this); }
    u32 m_ProtocolVersion;
};

// A host's answer to netQueryHostsMessage.
class netEngineHostInfoMessage : public netMessage {
public:
    netEngineHostInfoMessage(u16 type = kNET_HOST_INFO)
        : netMessage(type), m_HostIndex(0), m_NumPeers(0), m_MaxPeers(0), m_Phase(0), m_Sealed(0), m_HasPassword(0), m_ProtocolVersion(0) {
        m_Length = sizeof(*this);
        m_HostName[0] = 0;
    }
    netAddress m_Address;       // host address (filled by the receiver from the packet source)
    netGUID m_HostGUID;
    _TCHAR m_HostName[net::kMaxNameLength];
    int m_HostIndex;
    int m_NumPeers;
    int m_MaxPeers;
    int m_Phase;
    u8 m_Sealed;
    u8 m_HasPassword;
    u32 m_ProtocolVersion;
};

class netEngineJoinRequestMessage : public netMessage {
public:
    netEngineJoinRequestMessage(u16 type = kNET_JOIN_REQUEST)
        : netMessage(type), m_Phase(0), m_ProtocolVersion(0) {
        m_Length = sizeof(*this);
        m_Name[0] = 0;
        m_Password[0] = 0;
    }
    _TCHAR m_Name[net::kMaxNameLength];
    char m_Password[net::kMaxPasswordLength];
    netGUID m_GUID;
    int m_Phase;
    u32 m_ProtocolVersion;
};

// Host -> joiner: your slot, the host's slot and the peers already present.
class netEngineWelcomeMessage : public netMessage {
public:
    struct PeerEntry {
        netAddress m_Address;
        netGUID m_GUID;
        _TCHAR m_Name[net::kMaxNameLength];
        int m_Phase;
        u8 m_Valid;
    };
    enum { kMaxPeerEntries = 16 };

    netEngineWelcomeMessage(u16 type = kNET_WELCOME)
        : netMessage(type), m_PeerIndex(-1), m_HostIndex(0), m_MaxPeers(0), m_NumPeers(0), m_Phase(0) {
        m_Length = sizeof(*this);
        for (int i = 0; i < kMaxPeerEntries; i++) { m_Peers[i].m_Valid = 0; m_Peers[i].m_Name[0] = 0; m_Peers[i].m_Phase = 0; }
    }
    int m_PeerIndex;            // the joiner's new index
    int m_HostIndex;
    int m_MaxPeers;
    int m_NumPeers;
    int m_Phase;                // session phase
    netAddress m_Address;       // the joiner's address as the host sees it (public address)
    PeerEntry m_Peers[kMaxPeerEntries];
};

// Host -> everyone else: a peer joined.
class netEngineNewJoinMessage : public netMessage {
public:
    netEngineNewJoinMessage(u16 type = kNET_NEW_JOIN)
        : netMessage(type), m_PeerIndex(-1), m_Phase(0) {
        m_Length = sizeof(*this);
        m_Name[0] = 0;
    }
    int m_PeerIndex;
    netAddress m_Address;
    netGUID m_GUID;
    _TCHAR m_Name[net::kMaxNameLength];
    int m_Phase;
};

// Sent by a leaving peer, by the host to eject somebody, or by a peer that
// refused a join (m_PeerIndex == -1, m_Reason says why).
class netQuitMessage : public netMessage {
public:
    netQuitMessage() : netMessage(kNET_QUIT), m_PeerIndex(-1), m_Reason(0), m_NewHostIndex(-1) { m_Length = sizeof(*this); }
    int m_PeerIndex;
    int m_Reason;
    int m_NewHostIndex;     // set by a quitting host: who takes over
};

class netPingMessage : public netMessage {
public:
    netPingMessage(u16 type = kNET_PING) : netMessage(type), m_SendTime(0), m_PeerIndex(-1) { m_Length = sizeof(*this); }
    u32 m_SendTime;         // ping: sender's clock; pong: echoed ping send time
    u32 m_RemoteTime;       // pong: responder's clock when answering
    int m_PeerIndex;        // sender's index
};

// Phase messages: request (peer -> host), session (host -> all), peer (peer -> all).
class netPhaseMessage : public netMessage {
public:
    netPhaseMessage(u16 type = kNET_PHASE) : netMessage(type), m_Phase(0), m_PeerIndex(-1) { m_Length = sizeof(*this); }
    int m_Phase;
    int m_PeerIndex;
};

// thread -> manager events

class netResetMessage : public netMessage {
public:
    netResetMessage() : netMessage(kNET_EVT_RESET) { m_Length = sizeof(*this); }
};

class netPeerAddedMessage : public netMessage {
public:
    netPeerAddedMessage() : netMessage(kNET_EVT_PEER_ADDED), m_PeerIndex(-1), m_MyIndex(-1), m_HostIndex(-1), m_Phase(0) { m_Length = sizeof(*this); m_Name[0] = 0; }
    int m_PeerIndex;
    int m_MyIndex;
    int m_HostIndex;
    int m_Phase;
    netGUID m_GUID;
    netAddress m_Address;
    _TCHAR m_Name[net::kMaxNameLength];
};

class netPeerRemovedMessage : public netMessage {
public:
    netPeerRemovedMessage() : netMessage(kNET_EVT_PEER_REMOVED), m_PeerIndex(-1), m_Reason(0), m_OldMyIndex(-1), m_OldHostIndex(-1), m_NewHostIndex(-1) { m_Length = sizeof(*this); m_Name[0] = 0; }
    int m_PeerIndex;
    int m_Reason;           // net::eQuitReason
    int m_OldMyIndex;
    int m_OldHostIndex;
    int m_NewHostIndex;     // -1 when the session is gone
    _TCHAR m_Name[net::kMaxNameLength];
};

class netPhaseChangedMessage : public netMessage {
public:
    netPhaseChangedMessage() : netMessage(kNET_EVT_PHASE_CHANGED), m_PeerIndex(-1), m_Phase(0) { m_Length = sizeof(*this); }
    int m_PeerIndex;        // -1: session phase
    int m_Phase;
};

// 2.72 spellings the game uses
typedef netEngineHostInfoMessage netHostInfoMessage;
typedef netEngineJoinRequestMessage netJoinRequestMessage;
typedef netEngineWelcomeMessage netWelcomeMessage;
typedef netEngineNewJoinMessage netNewJoinMessage;

#endif // NET_ENGINE_NETENGINEMESSAGES_H
