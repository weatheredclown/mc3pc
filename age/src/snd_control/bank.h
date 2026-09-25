////////////////////////////////////////
// bank.h
////////////////////////////////////////

#ifndef SND_CONTROL_BANK_H
#define SND_CONTROL_BANK_H

#include "core/output.h"
#include "core/types.h"
#include "snd_control/bankentry.h"

// Bank type flags
enum { kSfx = 0, kStream = 1, kSequence = 2 };

class sndBank {
public:
    enum { MAX_ENTRIES = 96, MAX_VAGS = 256 };

    sndBank();
    sndBank(const char *name);
    ~sndBank();

    const char* GetName() const { return m_Name; }
    int GetNumSounds() const { return m_NumSounds; }
    int GetNumSlots() const { return m_NumSounds; } // slots same as sounds

    CBankEntry* GetEntry(int index) const;

    CBankEntry* AddSound(const char *soundName);
    CBankEntry* AddStream(const char *name, bool loop = false) { return AddSound(name); }
    static void ConvertName(char *dst, const char *src = nullptr) { if (dst && src) strcpy(dst, src); }

    // Load the .td descriptor + .bd sample data through the ASSET manager.
    // The caller is expected to have pushed the bank folder (Audio/banks).
    bool Load();

    // Decoded 16-bit mono PCM for one vag chunk (lazy ADPCM decode, cached).
    // Returns false if the index is out of range or the bank has no data.
    bool GetVagPcm(int vagIndex, const s16 **samples, int *numSamples, int *sampleRate);

    int GetNumVags() const { return m_NumVags; }
    bool GetVagLoop(int v) const { return (v >= 0 && v < m_NumVags) ? m_VagLoop[v] : false; }
    void PreloadAllVags();

    // Reference counting for shared banks.  The UI, HUD and surface banks are
    // used by both the frontend and a race, so each part of the game that
    // wants one claims it and lets it go again; the samples only have to be
    // resident while somebody is holding it.  `loadNow` asks for the sample
    // data to be brought in on the claim that takes the count from zero to
    // one, which is the claim that makes the bank resident.
    void AddUser(bool loadNow = true);
    void RemoveUser(bool freeNow = true);
    int GetNumUsers() const { return m_Users; }

    // Drops the decoded PCM cache, keeping the descriptor and the raw sample
    // data (a later user decodes again on demand).
    void FreeDecodedSamples();

private:
    void ScanVagChunks();

    char m_Name[64];
    int m_Users;            // see AddUser / RemoveUser
    int m_NumSounds;
    CBankEntry* m_Entries[MAX_ENTRIES];

    // Raw .bd payload (headerless concatenated PS2 SPU ADPCM streams) and the
    // chunk table: preferred source is the companion .hd (Sony SCEI header
    // with per-subsong offset/rate/flags); fallback derives chunks from the
    // SPU end-flag bits at a default rate.
    bool LoadHdTable();
    // MC3 (TD_FILE 3.0): the samples live in <bank>.bnk, a Sony "SBlk" v3 bank at the asset root (BANKS.DAT).
    bool LoadBnk();

    u8  *m_BdData;
    int  m_BdSize;
    int  m_NumVags;
    int  m_VagOffset[MAX_VAGS];
    int  m_VagLength[MAX_VAGS];
    int  m_VagRate[MAX_VAGS];
    bool m_VagLoop[MAX_VAGS];

    // Lazy per-vag decoded PCM cache.
    s16 *m_VagPcm[MAX_VAGS];
    int  m_VagPcmSamples[MAX_VAGS];
};

class sndBankManager {
public:
    static sndBankManager *sm_Instance;

    sndBankManager();
    ~sndBankManager();

    class sndBank *LoadBank(const char *name, int type = 0, bool delayLoading = false);   // the loaded (or already resident) bank
    void UnloadBank(const char *name);
    // AGE 2.72: capacity hint (banks are hashed by name), empty runtime
    // banks (stream/sequence containers filled by the game), and the
    // debug walk that reports gaps in the bank table.
    void SetMaxNumBanks(int max) { m_MaxNumBanks = max; }
    sndBank* CreateEmptyBank(const char *name, int type);
    // Reports gaps in the bank table.  UnloadBank leaves a hole where it
    // removed a bank, and a hole means a later LoadBank silently reuses that
    // slot while GetBankName(index) still walks past it, so the game checks
    // for them around layer transitions.
    void DebugCheckForBankHoles();

    int GetNumBanks() const { return m_NumBanks; }
    const char* GetBankName(int index) const;

    sndBank* FindBank(const char *name) const;
    sndBankEntry* FindEntry(const char *name) const;

private:
    int m_NumBanks;
    int m_MaxNumBanks;
    sndBank* m_Banks[32];
};

#define SNDBANKMGR (sndBankManager::sm_Instance)

class sndStream {
public:
    static bool LocateStream(const char *name, int &offset, int &length) {
        offset = 0;
        length = 0;
        return false;
    }
    static bool LocateStream(const char *name, u32 &offset, u32 &length) {
        offset = 0;
        length = 0;
        return false;
    }
};

#endif // SND_CONTROL_BANK_H
