#ifndef NET2_NETMESSAGES_H
#define NET2_NETMESSAGES_H

////////////////////////////////////////
// net2/netmessages.h
//
// Wire format.  A datagram is one netPacketHeader followed by
// m_NumMessages netMessage records laid end to end; every message starts
// with the netMessage header (command, byte length, time stamp, reliable
// sequence) and game messages append their own fields.  All fields travel
// in host byte order (both ends run the same build).
////////////////////////////////////////

#include "core/types.h"
#include "net_engine/netengineconsts.h"

struct netPacketHeader {
    enum { UNENCRYPTED = 0, ENCRYPTED = 1 };
    enum { kMagic = 0xA5 };

    u8  m_Magic;
    u8  m_PacketType;      // UNENCRYPTED / ENCRYPTED
    u8  m_NumMessages;
    u8  m_Pad;
    short m_Length;        // bytes including this header
    u16 m_Ack;             // highest reliable sequence received from the destination (0 = none)

    netPacketHeader() : m_Magic(kMagic), m_PacketType(UNENCRYPTED), m_NumMessages(0), m_Pad(0), m_Length(sizeof(netPacketHeader)), m_Ack(0) {}
};

class netMessage {
public:
    enum { kMaxMessageSize = 1280 };

    u16 m_Type;            // command (engine commands < kGAME_COMMAND_0)
    u16 m_Sequence;        // reliable sequence number (0 = unreliable)
    int m_Length;          // bytes including this header
    u32 m_TimeStamp;       // sender's GetTimeStamp() when queued

    netMessage(u16 type = 0) : m_Type(type), m_Sequence(0), m_Length(sizeof(netMessage)), m_TimeStamp(0) {}
    virtual ~netMessage() {}

    u16 GetCommand() const { return m_Type; }
    int GetLength() const { return m_Length; }
    void SetLength(int length) { m_Length = length; }
    bool IsReliable() const { return m_Sequence != 0; }

    // Bytes that go on the wire: everything after the vtable pointer.
    const unsigned char *WireData() const { return (const unsigned char *)&m_Type; }
    unsigned char *WireData() { return (unsigned char *)&m_Type; }
    int WireLength() const { return m_Length - (int)sizeof(void *); }
    static int WireHeaderLength() { return (int)(sizeof(netMessage) - sizeof(void *)); }
};

#endif // NET2_NETMESSAGES_H
