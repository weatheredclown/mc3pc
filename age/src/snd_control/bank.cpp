////////////////////////////////////////
// bank.cpp
//
// Sound bank loading. A bank is a .td text descriptor plus a .bd binary
// payload, both under Audio/banks/ and read through the ASSET manager (so
// plain -path trees and -archive DAT mounts both work):
//
//   .td:  TD_FILE 1.62
//         NUMPROG <n>          n programs, each:
//           PROGRAM <name>
//           NUMSPLT <n>        n splits (the names gameplay looks up), each:
//             <split name>
//             <int a> <int b> <int c>     (see MapSplitToVags below)
//         NUMPARM <n> + names  per-voice tuning entries (unused for now)
//         NUMVAGS <n> + names  the source sample list, in .bd order
//
//   MC3 (TD_FILE 3.0):
//         NUMSOUNDS <n>        n sounds, each: <name> <sound index> <loop flag>
//         The index is the sound's slot in <bank>.bnk, a Sony "SBlk" v3 bank
//         (BANKS.DAT, mounted at the asset root).  Layout per the vgmstream
//         bnk_sony reference, verified against the shipped banks:
//           file: u32 version(3), sections, sblk_off, sblk_size, data_off, data_size
//           SBlk @sblk_off: "SBlk", u32 version(3), u32 flags,
//             u16 sounds @0x16, u16 grains @0x18, u16 streams @0x1a,
//             u32 sound table @0x1c, grain table @0x20, stream table @0x34 (all rel. to sblk_off)
//           sound entry (0x0c): u32 ?, u16 grain count, u16 flags (bit0 loop), u32 byte offset of its
//             first grain in the grain table (-8 = no grains)
//           grain (0x08): u32 value; value>>16 == 0x0100 plays the stream header at (value & 0xffff)
//           stream header (0x18): u8 center_note @2, center_fine @3, u16 flags @0xe,
//             u32 stream offset @0x10 (rel. to data_off), u32 size @0x14 (0: runs to the next stream)
//           sample rate: SPU2 pitch of middle C against the centre note, 0x1000 = 48 kHz
//         Payload is the same SPU ADPCM as .bd.
//
//   .bd:  headerless concatenation of PS2 SPU ADPCM ("VAG") payloads. Each
//         16-byte block is [shift/filter byte][flag byte][14 nibble bytes];
//         a block with (flags & 1) ends a chunk. Chunk count matches NUMVAGS.
////////////////////////////////////////

#include "snd_control/bank.h"
#include "core/output.h"
#include "core/stream.h"
#include "data/assetcfg.h"
#include "data/args.h"
#include "data/token.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string>
#include "atl/array.h"

sndBankManager *sndBankManager::sm_Instance = NULL;
static sndBankManager s_BankManager;

// ---------------------------------------------------------------------------
// PS2 SPU ADPCM decode (the "VAG" codec). Standard filter table, /64 fixed
// point, 28 samples per 16-byte block.
// ---------------------------------------------------------------------------

static const int sVagF0[5] = { 0, 60, 115, 98, 122 };
static const int sVagF1[5] = { 0, 0, -52, -55, -60 };

// The prototype's PS2 sound tool exported SFX at 22.05kHz (chunk durations
// line up with the source .VAG names at this rate).
static const int kVagSampleRate = 22050;

static int DecodeVagChunk(const u8 *data, int length, s16 *out /*may be NULL*/)
{
    int hist1 = 0, hist2 = 0;
    int numSamples = 0;

    for (int block = 0; block + 16 <= length; block += 16)
    {
        int shift  = data[block] & 0x0F;
        int filter = (data[block] >> 4) & 0x0F;
        if (filter > 4) filter = 0;   // corrupt block: don't index off the table
        if (shift > 12) shift = 9;

        for (int i = 0; i < 28; i++)
        {
            int nibble = data[block + 2 + (i >> 1)];
            nibble = (i & 1) ? (nibble >> 4) : (nibble & 0x0F);
            if (nibble & 8) nibble -= 16;                  // sign-extend 4 bits

            int sample = (nibble << 12) >> shift;
            sample += (hist1 * sVagF0[filter] + hist2 * sVagF1[filter] + 32) >> 6;

            if (sample >  32767) sample =  32767;
            if (sample < -32768) sample = -32768;

            hist2 = hist1;
            hist1 = sample;

            if (out) out[numSamples] = (s16)sample;
            numSamples++;
        }
    }
    return numSamples;
}

// ---------------------------------------------------------------------------
// sndBank
// ---------------------------------------------------------------------------

sndBank::sndBank() {
    m_Name[0] = '\0';
    m_NumSounds = 0;
    memset(m_Entries, 0, sizeof(m_Entries));
    m_Users = 0;
    m_BdData = NULL;
    m_BdSize = 0;
    m_NumVags = 0;
    memset(m_VagOffset, 0, sizeof(m_VagOffset));
    memset(m_VagLength, 0, sizeof(m_VagLength));
    for (int i = 0; i < MAX_VAGS; i++) m_VagRate[i] = kVagSampleRate;
    memset(m_VagLoop, 0, sizeof(m_VagLoop));
    memset(m_VagPcm, 0, sizeof(m_VagPcm));
    memset(m_VagPcmSamples, 0, sizeof(m_VagPcmSamples));
}

sndBank::sndBank(const char *name) {
    *this = sndBank();
    strncpy(m_Name, name ? name : "", sizeof(m_Name) - 1);
    m_Name[sizeof(m_Name) - 1] = '\0';
}

sndBank::~sndBank() {
    for (int i = 0; i < m_NumSounds; i++)
        delete m_Entries[i];
    for (int i = 0; i < MAX_VAGS; i++)
        delete [] m_VagPcm[i];
    delete [] m_BdData;
}

void sndBank::AddUser(bool loadNow) {
    ++m_Users;
    // The claim that takes the count from nothing to one is what makes the
    // bank resident; later claims just share it.
    if (m_Users == 1 && loadNow && !m_BdData)
        Load();
}

void sndBank::RemoveUser(bool freeNow) {
    if (m_Users > 0) --m_Users;
    // The last user going releases the decoded audio, which is the bulk of
    // what a bank costs; the descriptor stays so the next user does not have
    // to re-read the file.
    if (m_Users == 0 && freeNow)
        FreeDecodedSamples();
}

void sndBank::FreeDecodedSamples() {
    for (int i = 0; i < MAX_VAGS; i++) {
        delete [] m_VagPcm[i];
        m_VagPcm[i] = NULL;
        m_VagPcmSamples[i] = 0;
    }
}

CBankEntry* sndBank::GetEntry(int index) const {
    if (index >= 0 && index < m_NumSounds)
        return m_Entries[index];
    return NULL;
}

CBankEntry* sndBank::AddSound(const char *soundName) {
    if (m_NumSounds >= MAX_ENTRIES)
        return NULL;
    CBankEntry *e = new CBankEntry(soundName);
    e->SetBank(this);
    m_Entries[m_NumSounds++] = e;
    return e;
}

// The three integers per split are (vag_index, parm_index, loop_flag):
// vag_index = 0-based index into the NUMVAGS list (== .bd chunk order, and
// the .hd subsong order); parm_index = index into NUMPARM (per-voice SPU
// tuning, unused here — the Oni2Rebuilt reference drops it too); loop_flag
// per the .td (the .hd flags bit 0 is the authoritative loop marker).

void sndBank::ScanVagChunks() {
    m_NumVags = 0;
    int start = 0;
    for (int off = 0; off + 16 <= m_BdSize; off += 16) {
        if (m_BdData[off + 1] & 0x01) {   // SPU end flag closes a chunk
            if (m_NumVags < MAX_VAGS) {
                m_VagOffset[m_NumVags] = start;
                m_VagLength[m_NumVags] = off + 16 - start;
                m_NumVags++;
            }
            start = off + 16;
        }
    }
    // Trailing data without an end flag: treat as one final chunk.
    if (start < m_BdSize && m_NumVags < MAX_VAGS) {
        m_VagOffset[m_NumVags] = start;
        m_VagLength[m_NumVags] = m_BdSize - start;
        m_NumVags++;
    }
}

// ---------------------------------------------------------------------------
// .hd — Sony SCEI bank header: authoritative per-subsong stream offset,
// sample rate and loop flag for the .bd payload. The prototype ships these
// in banks/BANKS.DAT next to the asset tree rather than in Audio/banks, so
// probe a few locations relative to the asset root. Layout (little-endian,
// magics are the byte-reversed chunk names):
//   0x00 "IECSsreV" (VersSCEI)     0x10 "IECSdaeH" (HeadSCEI)
//   0x1C hd_size   0x20 bd_size   0x30 vagi_offset
//   vagi: "IECSigaV", subsong count i32 @+0x0C, rel-pointer table @+0x10
//   entry: stream_offset u32 @+0, sample_rate u16 @+4, flags u8 @+6
//   (bit 0 = loop); size = next entry's offset - this one's (last: bd_size).
// ---------------------------------------------------------------------------

static u32 RdU32(const u8 *d, int size, int off) {
    if (off < 0 || off + 4 > size) return 0;
    return (u32)d[off] | ((u32)d[off+1] << 8) | ((u32)d[off+2] << 16) | ((u32)d[off+3] << 24);
}

static u8 *ReadWholeFile(const char *path, int *outSize) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    u8 *data = new u8[size > 0 ? size : 1];
    int got = (int)fread(data, 1, size, f);
    fclose(f);
    if (got <= 0) { delete [] data; return NULL; }
    *outSize = got;
    return data;
}

u8 *sndProbeSideFile(const char *dirName, const char *fileName, const char *ext, int *outSize) {
    // The .hd/.stm side data ships in extracted BANKS.DAT/STREAMS.DAT dirs as
    // SIBLINGS of the asset root ("<...>/zips/banks", "<...>/zips/streams"),
    // so probe relative to whichever root this run was launched with:
    // -path <assets> (ASSET.m_RootPath) or -archive -archivepath <oni2>.
    char path[512];

    if (ASSET.m_RootPath[0] != '\0') {
        const char *rootForms[] = {
            "%s\\..\\%s\\%s.%s",
            "%s\\..\\%s\\%s.DAT\\%s.%s",   // handled specially below
        };
        sprintf(path, "%s\\..\\%s\\%s.%s", ASSET.m_RootPath, dirName, fileName, ext);
        for (char *c = path; *c; c++) if (*c == '/') *c = '\\';
        u8 *d = ReadWholeFile(path, outSize);
        if (d) return d;
        char upper[32];
        int i = 0;
        for (; dirName[i] && i < 31; i++) upper[i] = (char)toupper((unsigned char)dirName[i]);
        upper[i] = '\0';
        sprintf(path, "%s\\..\\%s\\%s.DAT\\%s.%s", ASSET.m_RootPath, dirName, upper, fileName, ext);
        for (char *c = path; *c; c++) if (*c == '/') *c = '\\';
        d = ReadWholeFile(path, outSize);
        if (d) return d;
        (void)rootForms;
    }

    const char *archivePath = NULL;
    if (ARGS.Get("archivepath", 0, &archivePath) && archivePath && archivePath[0]) {
        sprintf(path, "%s\\zips\\%s\\%s.%s", archivePath, dirName, fileName, ext);
        for (char *c = path; *c; c++) if (*c == '/') *c = '\\';
        u8 *d = ReadWholeFile(path, outSize);
        if (d) return d;
        char upper[32];
        int i = 0;
        for (; dirName[i] && i < 31; i++) upper[i] = (char)toupper((unsigned char)dirName[i]);
        upper[i] = '\0';
        sprintf(path, "%s\\zips\\%s\\%s.DAT\\%s.%s", archivePath, dirName, upper, fileName, ext);
        for (char *c = path; *c; c++) if (*c == '/') *c = '\\';
        d = ReadWholeFile(path, outSize);
        if (d) return d;
    }
    return NULL;
}

static u8 *ReadAuxFile(const char *bankName, const char *ext, int *outSize) {
    return sndProbeSideFile("banks", bankName, ext, outSize);
}

bool sndBank::LoadHdTable() {
    int size = 0;
    u8 *hd = ReadAuxFile(m_Name, "hd", &size);
    if (!hd) {
        char lower[64];
        int i = 0;
        for (; m_Name[i] && i < 63; i++) lower[i] = (char)tolower((unsigned char)m_Name[i]);
        lower[i] = '\0';
        hd = ReadAuxFile(lower, "hd", &size);
    }
    if (!hd)
        return false;

    bool ok = false;
    if (size >= 0x24 &&
        memcmp(hd, "IECSsreV", 8) == 0 &&
        memcmp(hd + 0x10, "IECSdaeH", 8) == 0)
    {
        u32 bdSize = RdU32(hd, size, 0x10 + 0x10);
        int vagi = (int)RdU32(hd, size, 0x10 + 0x20);

        if (vagi > 0 && vagi + 0x10 <= size && memcmp(hd + vagi, "IECSigaV", 8) == 0) {
            int count = (int)RdU32(hd, size, vagi + 0x0C);

            // vgmstream terminator heuristics (mirrors the Rust reference):
            // a non-null pointer just past the table adds a phantom entry; a
            // final entry whose stream offset equals bd_size is a terminator.
            if (vagi + 0x10 + 4 * count + 4 <= size &&
                RdU32(hd, size, vagi + 0x10 + 4 * count) != 0)
                count++;
            if (count > 0) {
                u32 lastRel = RdU32(hd, size, vagi + 0x10 + 4 * (count - 1));
                if (lastRel > 0 && RdU32(hd, size, vagi + (int)lastRel) == bdSize)
                    count--;
            }

            if (count > 0) {
                if (count > MAX_VAGS) count = MAX_VAGS;
                m_NumVags = count;
                for (int v = 0; v < count; v++) {
                    int rel = (int)RdU32(hd, size, vagi + 0x10 + 4 * v);
                    int info = vagi + rel;
                    u32 off = RdU32(hd, size, info);
                    u32 rate = (u32)hd[info + 4] | ((u32)hd[info + 5] << 8);
                    u8 flags = (info + 6 < size) ? hd[info + 6] : 0;
                    u32 next = (v == count - 1)
                        ? bdSize
                        : RdU32(hd, size, vagi + (int)RdU32(hd, size, vagi + 0x10 + 4 * (v + 1)));
                    m_VagOffset[v] = (int)off;
                    m_VagLength[v] = (next > off) ? (int)(next - off) : 0;
                    m_VagRate[v] = (rate >= 4000 && rate <= 48000) ? (int)rate : kVagSampleRate;
                    m_VagLoop[v] = (flags & 1) != 0;
                    if (m_VagOffset[v] + m_VagLength[v] > m_BdSize)
                        m_VagLength[v] = (m_VagOffset[v] < m_BdSize) ? m_BdSize - m_VagOffset[v] : 0;
                }
                ok = true;
            }
        }
    }

    delete [] hd;
    return ok;
}

static bool MatchVagName(const char *splitName, const char *vagName) {
    const char *a = splitName;
    const char *b = vagName;
    while (*a && *b && *b != '.') {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return false;
        a++;
        b++;
    }
    if (*a == '\0' && (*b == '\0' || *b == '.'))
        return true;
    return false;
}

static int FindVagByName(const char *splitName, const atArray<std::string> &vagNames) {
    if (vagNames.GetCount() == 0) return -1;
    // 1. Direct match
    for (int i = 0; i < vagNames.GetCount(); ++i) {
        if (MatchVagName(splitName, vagNames[i].c_str()))
            return i;
    }
    // 2. KNK <-> DMG alias match (foley walk/jog sounds shared between characters)
    char alias[128];
    strncpy(alias, splitName, sizeof(alias) - 1);
    alias[sizeof(alias) - 1] = '\0';
    char *knk = strstr(alias, "_KNK_");
    if (!knk) knk = strstr(alias, "_knk_");
    if (!knk) knk = strstr(alias, "_Knk_");
    if (knk) {
        memcpy(knk, "_Dmg_", 5);
        for (int i = 0; i < vagNames.GetCount(); ++i) {
            if (MatchVagName(alias, vagNames[i].c_str()))
                return i;
        }
        memcpy(knk, "_DMG_", 5);
        for (int i = 0; i < vagNames.GetCount(); ++i) {
            if (MatchVagName(alias, vagNames[i].c_str()))
                return i;
        }
    }
    char *dmg = strstr(alias, "_Dmg_");
    if (!dmg) dmg = strstr(alias, "_DMG_");
    if (!dmg) dmg = strstr(alias, "_dmg_");
    if (dmg) {
        memcpy(dmg, "_KNK_", 5);
        for (int i = 0; i < vagNames.GetCount(); ++i) {
            if (MatchVagName(alias, vagNames[i].c_str()))
                return i;
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------
// SPU2 pitch math (documented in the vgmstream / OpenGOAL references): a
// sample's playback rate is the SPU pitch of middle C (note 60) relative to
// the stored centre note/fine, with 0x1000 = 48 kHz.  A centre note with the
// top bit set is stored negated and skips the 44.1 kHz rescale.
// ---------------------------------------------------------------------------

static const u16 sNotePitchTable[12] = {
    0x8000, 0x879C, 0x8FAC, 0x9837, 0xA145, 0xAADC,
    0xB504, 0xBFC8, 0xCB2F, 0xD744, 0xE411, 0xF1A1
};

static const u16 sFinePitchTable[128] = {
    0x8000, 0x800E, 0x801D, 0x802C, 0x803B, 0x804A, 0x8058, 0x8067,
    0x8076, 0x8085, 0x8094, 0x80A3, 0x80B1, 0x80C0, 0x80CF, 0x80DE,
    0x80ED, 0x80FC, 0x810B, 0x811A, 0x8129, 0x8138, 0x8146, 0x8155,
    0x8164, 0x8173, 0x8182, 0x8191, 0x81A0, 0x81AF, 0x81BE, 0x81CD,
    0x81DC, 0x81EB, 0x81FA, 0x8209, 0x8218, 0x8227, 0x8236, 0x8245,
    0x8254, 0x8263, 0x8272, 0x8282, 0x8291, 0x82A0, 0x82AF, 0x82BE,
    0x82CD, 0x82DC, 0x82EB, 0x82FA, 0x830A, 0x8319, 0x8328, 0x8337,
    0x8346, 0x8355, 0x8364, 0x8374, 0x8383, 0x8392, 0x83A1, 0x83B0,
    0x83C0, 0x83CF, 0x83DE, 0x83ED, 0x83FD, 0x840C, 0x841B, 0x842A,
    0x843A, 0x8449, 0x8458, 0x8468, 0x8477, 0x8486, 0x8495, 0x84A5,
    0x84B4, 0x84C3, 0x84D3, 0x84E2, 0x84F1, 0x8501, 0x8510, 0x8520,
    0x852F, 0x853E, 0x854E, 0x855D, 0x856D, 0x857C, 0x858B, 0x859B,
    0x85AA, 0x85BA, 0x85C9, 0x85D9, 0x85E8, 0x85F8, 0x8607, 0x8617,
    0x8626, 0x8636, 0x8645, 0x8655, 0x8664, 0x8674, 0x8683, 0x8693,
    0x86A2, 0x86B2, 0x86C1, 0x86D1, 0x86E0, 0x86F0, 0x8700, 0x870F,
    0x871F, 0x872E, 0x873E, 0x874E, 0x875D, 0x876D, 0x877D, 0x878C
};

static int ClampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static int PsNoteToPitch(int centerNote, int centerFine, int note, int fine) {
    int fineIdx = fine + centerFine;
    int fineAdjust = fineIdx;
    if (fineIdx < 0)
        fineAdjust = fineIdx + 0x7F;
    fineAdjust /= 128;
    int noteAdjust = note + fineAdjust - centerNote;
    int octave = noteAdjust / 6;
    if (noteAdjust < 0)
        octave--;
    fineIdx -= fineAdjust * 128;
    int neg = noteAdjust < 0 ? -1 : 0;
    if (octave < 0)
        octave--;
    int base = (octave / 2) - neg;
    int shift = base - 2;
    int noteIdx = noteAdjust - (base * 12);
    if (noteIdx < 0 || (noteIdx == 0 && fineIdx < 0)) {
        noteIdx += 12;
        shift = base - 3;
    }
    if (fineIdx < 0) {
        noteIdx = (noteIdx - 1) + fineAdjust;
        fineIdx += (fineAdjust + 1) * 128;
    }
    noteIdx = ClampInt(noteIdx, 0, 11);
    fineIdx = ClampInt(fineIdx, 0, 127);
    int pitch = ((int)sNotePitchTable[noteIdx] * (int)sFinePitchTable[fineIdx]) >> 16;
    if (shift < 0)
        pitch = (pitch + (1 << (-shift - 1))) >> -shift;
    return pitch & 0xFFFF;
}

static int Spu2CenterNoteToSampleRate(u8 centerNote, u8 centerFine) {
    bool negative = (centerNote >> 7) != 0;
    int cn = negative ? 0x100 - centerNote : centerNote;
    int pitch = PsNoteToPitch(cn, centerFine, 60, 0);
    if (pitch > 0x4000)
        pitch = 0x4000;
    if (!negative)
        pitch = (pitch * 44100) / 48000;
    return (48000 * pitch) / 4096;
}

// Reads <bank>.bnk (Sony SBlk v3) and fills the per-sound sample table:
// sound index -> stream offset/length/rate/loop.  Sounds without a waveform
// grain get an empty chunk.
bool sndBank::LoadBnk() {
    Stream *f = ASSET.Open(m_Name, "bnk");
    if (!f) {
        // BANKS.DAT is mounted at the asset root, above Audio/banks.
        ASSET.PopFolder();
        f = ASSET.Open(m_Name, "bnk");
        if (!f) {
            char lower[64];
            int i = 0;
            for (; m_Name[i] && i < 63; i++) lower[i] = (char)tolower((unsigned char)m_Name[i]);
            lower[i] = '\0';
            f = ASSET.Open(lower, "bnk");
        }
        ASSET.PushFolder("Audio/banks");
    }
    if (!f)
        return false;

    int size = f->Size();
    u8 *bnk = NULL;
    if (size > 0) {
        bnk = new u8[size];
        int got = f->Read(bnk, size);
        size = got > 0 ? got : 0;
    } else {
        int cap = 256 * 1024, total = 0;
        u8 *buf = new u8[cap];
        for (;;) {
            if (total == cap) {
                u8 *grown = new u8[cap * 2];
                memcpy(grown, buf, total);
                delete [] buf;
                buf = grown;
                cap *= 2;
            }
            int got = f->Read(buf + total, cap - total);
            if (got <= 0) break;
            total += got;
        }
        bnk = buf;
        size = total;
    }
    f->Close();

    bool ok = false;
    do {
        if (size < 0x18) break;
        u32 version = RdU32(bnk, size, 0x00);
        int sblk = (int)RdU32(bnk, size, 0x08);
        int dataOff = (int)RdU32(bnk, size, 0x10);
        int dataSize = (int)RdU32(bnk, size, 0x14);
        if (version != 3 || sblk <= 0 || sblk + 0x3c > size || memcmp(bnk + sblk, "SBlk", 4) != 0) {
            Warningf("sndBank::LoadBnk: '%s' is not a version-3 SBlk bank", m_Name);
            break;
        }
        if (dataOff < 0 || dataOff > size) break;
        if (dataOff + dataSize > size) dataSize = size - dataOff;

        int numSounds = (int)bnk[sblk + 0x16] | ((int)bnk[sblk + 0x17] << 8);
        int numGrains = (int)bnk[sblk + 0x18] | ((int)bnk[sblk + 0x19] << 8);
        int soundTab = sblk + (int)RdU32(bnk, size, sblk + 0x1c);
        int grainTab = sblk + (int)RdU32(bnk, size, sblk + 0x20);
        int streamTab = sblk + (int)RdU32(bnk, size, sblk + 0x34);
        if (numSounds > MAX_VAGS) numSounds = MAX_VAGS;

        // the payload becomes the bank's sample data
        m_BdData = new u8[dataSize > 0 ? dataSize : 1];
        memcpy(m_BdData, bnk + dataOff, dataSize);
        m_BdSize = dataSize;

        // every stream start, to bound sizes the header leaves at 0
        int starts[MAX_VAGS * 4];
        int numStarts = 0;

        for (int sIdx = 0; sIdx < numSounds; sIdx++) {
            int e = soundTab + sIdx * 0x0c;
            int grainCount = (int)bnk[e + 4] | ((int)bnk[e + 5] << 8);
            int soundFlags = (int)bnk[e + 6] | ((int)bnk[e + 7] << 8);
            int grainByteOff = (int)RdU32(bnk, size, e + 8);
            m_VagOffset[sIdx] = 0;
            m_VagLength[sIdx] = 0;
            m_VagRate[sIdx] = kVagSampleRate;
            m_VagLoop[sIdx] = (soundFlags & 1) != 0;
            if (grainByteOff < 0 || grainCount <= 0) continue;
            for (int g = 0; g < grainCount; g++) {
                int gOff = grainTab + grainByteOff + g * 8;
                if (gOff + 8 > size || (gOff - grainTab) / 8 >= numGrains) break;
                u32 value = RdU32(bnk, size, gOff);
                if ((value >> 16) != 0x0100) continue;
                int h = streamTab + (int)(value & 0xffff);
                if (h + 0x18 > size) break;
                u8 centerNote = bnk[h + 2], centerFine = bnk[h + 3];
                int streamOff = (int)RdU32(bnk, size, h + 0x10);
                int streamSize = (int)RdU32(bnk, size, h + 0x14);
                if (streamOff < 0 || streamOff >= dataSize) break;
                m_VagOffset[sIdx] = streamOff;
                m_VagLength[sIdx] = (streamSize > 0 && streamOff + streamSize <= dataSize) ? streamSize : 0;
                m_VagRate[sIdx] = Spu2CenterNoteToSampleRate(centerNote, centerFine);
                if (m_VagRate[sIdx] < 4000 || m_VagRate[sIdx] > 48000) m_VagRate[sIdx] = kVagSampleRate;
                if (numStarts < MAX_VAGS * 4) starts[numStarts++] = streamOff;
                break;      // first waveform grain plays the sound
            }
        }
        // unsized streams run to the next stream start (the decoder also stops at the SPU end flag)
        for (int sIdx = 0; sIdx < numSounds; sIdx++) {
            if (m_VagLength[sIdx] != 0) continue;
            bool hasStream = false;
            for (int k = 0; k < numStarts; k++) if (starts[k] == m_VagOffset[sIdx]) { hasStream = true; break; }
            if (!hasStream) continue;
            int next = dataSize;
            for (int k = 0; k < numStarts; k++)
                if (starts[k] > m_VagOffset[sIdx] && starts[k] < next) next = starts[k];
            m_VagLength[sIdx] = next - m_VagOffset[sIdx];
        }
        m_NumVags = numSounds;
        ok = true;
    } while (0);

    delete [] bnk;
    return ok;
}

bool sndBank::Load() {
    // ---- .td descriptor ----
    Stream *td = ASSET.Open(m_Name, "td");
    if (!td) {
        // Bank names in code aren't case-normalized; the files on disk are.
        char lower[64];
        int i = 0;
        for (; m_Name[i] && i < 63; i++) lower[i] = (char)tolower((unsigned char)m_Name[i]);
        lower[i] = '\0';
        td = ASSET.Open(lower, "td");
    }
    if (!td) {
        Warningf("sndBank::Load: no .td descriptor for bank '%s'", m_Name);
        return false;
    }

    datAsciiTokenizer tok;
    tok.Init(m_Name, td);

    char t[128];
    tok.MatchToken("TD_FILE");
    float tdVersion = tok.GetFloat();                 // 1.62 (rb) or 3.0 (MC3)

    struct TempSplit {
        char name[128];
        int splitIdx;
        int parmIdx;
        int loopFlag;
    };
    atArray<TempSplit> splits;

    int numProg = 0;
    if (tdVersion >= 2.999f) {
        // MC3 layout: NUMSOUNDS n, then name / sound index / loop flag per sound.
        int numSounds = tok.MatchInt("NUMSOUNDS");
        for (int i = 0; i < numSounds; i++) {
            TempSplit sp;
            tok.GetToken(sp.name, sizeof(sp.name));
            sp.splitIdx = tok.GetInt();
            sp.parmIdx = 0;
            sp.loopFlag = tok.GetInt();
            splits.Append(sp);
        }
    } else {
        numProg = tok.MatchInt("NUMPROG");
    }
    for (int p = 0; p < numProg; p++) {
        tok.MatchToken("PROGRAM");
        char progName[64];
        tok.GetToken(progName, sizeof(progName));

        int numSplit = tok.MatchInt("NUMSPLT");
        for (int s = 0; s < numSplit; s++) {
            TempSplit sp;
            tok.GetToken(sp.name, sizeof(sp.name));
            sp.splitIdx = tok.GetInt();
            sp.parmIdx = tok.GetInt();
            sp.loopFlag = tok.GetInt();
            splits.Append(sp);
        }
    }

    atArray<std::string> vagNames;
    if (tok.CheckToken("NUMPARM")) {
        int n = tok.GetInt();
        for (int i = 0; i < n; i++) tok.GetToken(t, sizeof(t));
    }
    int declaredVags = -1;
    if (tok.CheckToken("NUMVAGS")) {
        declaredVags = tok.GetInt();
        for (int i = 0; i < declaredVags; i++) {
            tok.GetToken(t, sizeof(t));
            vagNames.Append(std::string(t));
        }
    }
    td->Close();

    for (int i = 0; i < splits.GetCount(); ++i) {
        const TempSplit &sp = splits[i];
        int vagIndex = sp.splitIdx;
        if (vagNames.GetCount() > 0) {
            int matched = FindVagByName(sp.name, vagNames);
            if (matched >= 0) {
                vagIndex = matched;
            }
        }
        CBankEntry *e = AddSound(sp.name);
        if (e) {
            e->SetRawSplit(vagIndex, sp.parmIdx, sp.loopFlag);
            e->SetVagRange(vagIndex, vagIndex);
            e->SetLoop(sp.loopFlag != 0);
        }
    }

    // ---- MC3: .bnk payload ----
    if (tdVersion >= 2.999f) {
        if (!LoadBnk())
            Warningf("sndBank::Load: no .bnk sample data for bank '%s'", m_Name);
        Displayf("Loaded sound bank '%s': %d sounds, %d sample chunks.", m_Name, m_NumSounds, m_NumVags);
        return true;
    }

    // ---- .bd payload ----
    Stream *bd = ASSET.Open(m_Name, "bd");
    if (!bd) {
        char lower[64];
        int i = 0;
        for (; m_Name[i] && i < 63; i++) lower[i] = (char)tolower((unsigned char)m_Name[i]);
        lower[i] = '\0';
        bd = ASSET.Open(lower, "bd");
    }
    if (bd) {
        int size = bd->Size();
        if (size > 0) {
            m_BdData = new u8[size];
            int got = bd->Read(m_BdData, size);
            m_BdSize = got > 0 ? got : 0;
        } else {
            // Some stream backends (archive mounts) don't report Size();
            // read in chunks until EOF.
            int cap = 256 * 1024;
            u8 *buf = new u8[cap];
            int total = 0;
            for (;;) {
                if (total == cap) {
                    int newCap = cap * 2;
                    u8 *grown = new u8[newCap];
                    memcpy(grown, buf, total);
                    delete [] buf;
                    buf = grown;
                    cap = newCap;
                }
                int got = bd->Read(buf + total, cap - total);
                if (got <= 0) break;
                total += got;
            }
            m_BdData = buf;
            m_BdSize = total;
        }
        bd->Close();
        // Prefer the authoritative .hd table (exact offsets + per-sound
        // sample rates + loop flags); fall back to SPU end-flag scanning.
        if (!LoadHdTable())
            ScanVagChunks();
        if (declaredVags >= 0 && declaredVags != m_NumVags)
            Warningf("sndBank::Load: '%s' declares %d vags but table holds %d chunks",
                     m_Name, declaredVags, m_NumVags);
    } else {
        Warningf("sndBank::Load: no .bd sample data for bank '%s'", m_Name);
    }

    Displayf("Loaded sound bank '%s': %d sounds, %d sample chunks.", m_Name, m_NumSounds, m_NumVags);
    return true;
}

bool sndBank::GetVagPcm(int vagIndex, const s16 **samples, int *numSamples, int *sampleRate) {
    if (vagIndex < 0 || vagIndex >= m_NumVags || !m_BdData)
        return false;

    if (!m_VagPcm[vagIndex]) {
        const u8 *chunk = m_BdData + m_VagOffset[vagIndex];
        int n = DecodeVagChunk(chunk, m_VagLength[vagIndex], NULL);
        if (n <= 0)
            return false;
        m_VagPcm[vagIndex] = new s16[n];
        m_VagPcmSamples[vagIndex] = DecodeVagChunk(chunk, m_VagLength[vagIndex], m_VagPcm[vagIndex]);
    }

    *samples = m_VagPcm[vagIndex];
    *numSamples = m_VagPcmSamples[vagIndex];
    *sampleRate = m_VagRate[vagIndex];
    return true;
}

// ---------------------------------------------------------------------------
// sndBankManager
// ---------------------------------------------------------------------------

sndBankManager::sndBankManager() {
    m_MaxNumBanks = 32;
    sm_Instance = this;
    m_NumBanks = 0;
    memset(m_Banks, 0, sizeof(m_Banks));
}

sndBankManager::~sndBankManager() {
    for (int i = 0; i < m_NumBanks; i++)
        delete m_Banks[i];
    if (sm_Instance == this)
        sm_Instance = NULL;
}

void sndBank::PreloadAllVags() {
    for (int i = 0; i < m_NumVags; i++) {
        const s16 *samples = NULL;
        int numSamples = 0, sampleRate = 0;
        GetVagPcm(i, &samples, &numSamples, &sampleRate);
    }
}

sndBank *sndBankManager::LoadBank(const char *name, int type, bool delayLoading) {
    if (sndBank *existing = FindBank(name)) return existing;
    if (m_NumBanks >= 32) {
        Warningf("sndBankManager::LoadBank: bank table full, dropping '%s'", name);
        return NULL;
    }

    sndBank *bank = new sndBank(name);

    ASSET.PushFolder("Audio/banks");
    bool ok = bank->Load();
    ASSET.PopFolder();

    if (!ok)
        Warningf("sndBankManager::LoadBank: bank '%s' loaded empty", name);

    if (!delayLoading) {
        bank->PreloadAllVags();
    }

    m_Banks[m_NumBanks++] = bank;
    return bank;
}

void sndBankManager::DebugCheckForBankHoles() {
    // This port's UnloadBank compacts the table, so a hole means something
    // wrote a slot directly or a load failed halfway; report it rather than
    // let the gap surface later as a bank that cannot be found.
    int holes = 0;
    for (int i = 0; i < m_NumBanks; i++) {
        if (!m_Banks[i]) {
            Warningf("sndBankManager: bank slot %d of %d is empty", i, m_NumBanks);
            ++holes;
        }
    }
    for (int i = m_NumBanks; i < 32; i++) {
        if (m_Banks[i]) {
            Warningf("sndBankManager: bank slot %d holds '%s' past the end of the table (%d banks)",
                     i, m_Banks[i]->GetName(), m_NumBanks);
            ++holes;
        }
    }
    if (holes == 0)
        Displayf("sndBankManager: %d banks, no holes", m_NumBanks);
}

void sndBankManager::UnloadBank(const char *name) {
    for (int i = 0; i < m_NumBanks; i++) {
        if (m_Banks[i] && _stricmp(m_Banks[i]->GetName(), name) == 0) {
            delete m_Banks[i];
            for (int j = i; j < m_NumBanks - 1; j++)
                m_Banks[j] = m_Banks[j+1];
            m_Banks[m_NumBanks - 1] = NULL;
            m_NumBanks--;
            break;
        }
    }
}

const char* sndBankManager::GetBankName(int index) const {
    if (index >= 0 && index < m_NumBanks && m_Banks[index])
        return m_Banks[index]->GetName();
    return NULL;
}

sndBank* sndBankManager::FindBank(const char *name) const {
    for (int i = 0; i < m_NumBanks; i++) {
        if (m_Banks[i] && _stricmp(m_Banks[i]->GetName(), name) == 0)
            return m_Banks[i];
    }
    return NULL;
}

sndBankEntry* sndBankManager::FindEntry(const char *name) const {
    if (!name || !name[0])
        return NULL;

    // "program:split" — the program qualifier narrows nothing today (split
    // names are unique in practice); strip it and search by split name.
    const char *colon = strchr(name, ':');
    const char *splitName = colon ? colon + 1 : name;

    for (int i = 0; i < m_NumBanks; i++) {
        sndBank *bank = m_Banks[i];
        if (!bank) continue;
        for (int j = 0; j < bank->GetNumSounds(); j++) {
            CBankEntry *entry = bank->GetEntry(j);
            if (entry && _stricmp(entry->GetName(), splitName) == 0)
                return (sndBankEntry*)entry;
        }
    }

    // TEMP DEBUG: dump the registry once on the first miss so a lookup
    // failure shows what names were actually stored.
    static bool sDumped = false;
    if (!sDumped) {
        sDumped = true;
        Warningf("FindEntry miss for '%s' (split '%s') — registry dump:", name, splitName);
        for (int i = 0; i < m_NumBanks; i++) {
            sndBank *bank = m_Banks[i];
            if (!bank) continue;
            for (int j = 0; j < bank->GetNumSounds() && j < 6; j++) {
                CBankEntry *entry = bank->GetEntry(j);
                Warningf("  bank '%s' [%d] = '%s'", bank->GetName(), j, entry ? entry->GetName() : "(null)");
            }
        }
    }
    return NULL;
}

// An empty bank the game fills at runtime (stream and sequence containers).
sndBank* sndBankManager::CreateEmptyBank(const char *name, int type) {
    if (!name) return NULL;
    sndBank *existing = FindBank(name);
    if (existing) return existing;
    if (m_NumBanks >= (int)(sizeof(m_Banks) / sizeof(m_Banks[0]))) return NULL;
    sndBank *bank = new sndBank(name);
    (void)type;
    m_Banks[m_NumBanks++] = bank;
    return bank;
}
