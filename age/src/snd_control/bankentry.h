////////////////////////////////////////
// bankentry.h
////////////////////////////////////////

#ifndef SND_CONTROL_BANKENTRY_H
#define SND_CONTROL_BANKENTRY_H

#include "core/types.h"

class sndBank;

class CBankEntry {
public:
    CBankEntry();
    CBankEntry(const char *name);
    virtual ~CBankEntry();

    const char* GetName() const { return m_Name; }

    // Sample resolution — filled by sndBank::Load when the .td is parsed.
    sndBank*    GetBank() const        { return m_Bank; }
    int         GetVagFirst() const    { return m_VagFirst; }
    int         GetVagLast() const     { return m_VagLast; }
    bool        GetLoop() const        { return m_Loop; }

    void        SetBank(sndBank *bank) { m_Bank = bank; }
    void        SetVagRange(int first, int last) { m_VagFirst = first; m_VagLast = last; }
    void        SetLoop(bool loop)     { m_Loop = loop; }
    void        SetRawSplit(int a, int b, int c) { m_SplitA = a; m_SplitB = b; m_SplitC = c; }
    int         GetRawA() const        { return m_SplitA; }
    int         GetRawB() const        { return m_SplitB; }
    int         GetRawC() const        { return m_SplitC; }

protected:
    char m_Name[64];
    sndBank *m_Bank;
    int m_VagFirst;     // inclusive chunk range into the owner bank's .bd
    int m_VagLast;
    bool m_Loop;
    int m_SplitA, m_SplitB, m_SplitC;   // raw .td split integers
};

class sndBankSound : public CBankEntry {
public:
    sndBankSound();
    sndBankSound(const char *name);
    virtual ~sndBankSound() {}
};

class sndBankEntry : public CBankEntry {
public:
    sndBankEntry() : CBankEntry() {}
    sndBankEntry(const char *name) : CBankEntry(name) {}
};

#endif // SND_CONTROL_BANKENTRY_H
