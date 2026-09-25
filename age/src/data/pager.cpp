#include "data/pager.h"

#include "core/output.h"
#include "data/assetcfg.h"
#include <string.h>

#include "core/stream.h"

datPager PAGER;

// Global paging switch the game toggles around resource loads (no streaming yet).
bool datEnablePaging = false;

datPageManager *datPageManager::sm_Instance = 0;

datPageManager::datPageManager() : m_NumPages(0), m_PageSize(0)
{
    sm_Instance = this;
}

datPageManager::~datPageManager()
{
    if (sm_Instance == this) sm_Instance = 0;
}

datPageManager &datPageManager::GetInstance()
{
    if (!sm_Instance) new datPageManager;
    return *sm_Instance;
}

void datPageManager::AddPages(int size, int count)
{
    if (size > m_PageSize) m_PageSize = size;
    m_NumPages += count;
}

void datPageManager::RemovePages(int /*size*/, int count)
{
    m_NumPages -= count;
    if (m_NumPages < 0) m_NumPages = 0;
}

char datPageFile::sm_Names[kMaxSlots][128];
bool datPageFile::sm_Mounted[kMaxSlots];
static Stream *sm_Streams[datPageFile::kMaxSlots] = { 0 };
static int sm_NumEntries[datPageFile::kMaxSlots] = { 0 };
static u32 sm_DataStart[datPageFile::kMaxSlots] = { 0 };
static u32 *sm_Toc[datPageFile::kMaxSlots] = { 0 };

bool datPageFile::Mount(const char *name, int slot)
{
    if (slot < 0 || slot >= kMaxSlots || !name) return false;
    Unmount(slot);

    Stream *s = ASSET.Open(name, "ppf");
    if (!s) {
        Warningf("datPageFile::Mount: no '%s.ppf'", name);
        return false;
    }

    u8 hdr[12];
    if (s->Read(hdr, 12) != 12) {
        Warningf("datPageFile::Mount: '%s.ppf' too short", name);
        s->Close();
        return false;
    }

    if (hdr[0] != 'p' || hdr[1] != 'f' || hdr[2] != '0' || hdr[3] != '5') {
        Warningf("datPageFile::Mount: '%s.ppf' unknown magic (expected pf05)", name);
        s->Close();
        return false;
    }

    u32 numEntries = (u32)hdr[4] | ((u32)hdr[5] << 8) | ((u32)hdr[6] << 16) | ((u32)hdr[7] << 24);
    u32 dataStart = (u32)hdr[8] | ((u32)hdr[9] << 8) | ((u32)hdr[10] << 16) | ((u32)hdr[11] << 24);

    u32 *toc = 0;
    if (numEntries > 0 && numEntries < 65536) {
        toc = new u32[numEntries];
        if (s->Read(toc, (int)numEntries * 4) != (int)(numEntries * 4)) {
            Warningf("datPageFile::Mount: failed to read TOC in '%s.ppf'", name);
            delete [] toc;
            s->Close();
            return false;
        }
    }

    sm_Streams[slot] = s;
    sm_NumEntries[slot] = (int)numEntries;
    sm_DataStart[slot] = dataStart;
    sm_Toc[slot] = toc;

    strncpy(sm_Names[slot], name, sizeof(sm_Names[slot]) - 1);
    sm_Names[slot][sizeof(sm_Names[slot]) - 1] = 0;
    sm_Mounted[slot] = true;

    Displayf("datPageFile::Mount: '%s.ppf' in slot %d (pf05, %d entries, data @ 0x%08x)",
             name, slot, numEntries, dataStart);
    return true;
}

void datPageFile::Unmount(int slot)
{
    if (slot < 0 || slot >= kMaxSlots) return;
    if (sm_Streams[slot]) {
        sm_Streams[slot]->Close();
        sm_Streams[slot] = 0;
    }
    delete [] sm_Toc[slot];
    sm_Toc[slot] = 0;
    sm_NumEntries[slot] = 0;
    sm_DataStart[slot] = 0;
    sm_Mounted[slot] = false;
    sm_Names[slot][0] = 0;
}

bool datPageFile::IsMounted(int slot)
{
    return slot >= 0 && slot < kMaxSlots && sm_Mounted[slot];
}

const char *datPageFile::GetName(int slot)
{
    return (slot >= 0 && slot < kMaxSlots) ? sm_Names[slot] : "";
}

int datPageFile::GetNumEntries(int slot)
{
    return (slot >= 0 && slot < kMaxSlots) ? sm_NumEntries[slot] : 0;
}

u32 datPageFile::GetDataStart(int slot)
{
    return (slot >= 0 && slot < kMaxSlots) ? sm_DataStart[slot] : 0;
}

u32 datPageFile::GetEntrySector(int slot, int entryIndex)
{
    if (slot < 0 || slot >= kMaxSlots || entryIndex < 0 || entryIndex >= sm_NumEntries[slot] || !sm_Toc[slot]) return 0;
    return sm_Toc[slot][entryIndex] & 0xfffff;
}

bool datPageFile::ReadEntry(int slot, int entryIndex, u8 *outBuffer, u32 bufferSize, u32 *outPageSize)
{
    if (slot < 0 || slot >= kMaxSlots || !sm_Mounted[slot] || !sm_Streams[slot]) return false;
    if (entryIndex < 0 || entryIndex >= sm_NumEntries[slot] || !sm_Toc[slot]) return false;

    u32 entry = sm_Toc[slot][entryIndex];
    u32 sector = entry & 0xfffff;   // 20-bit sector: decal.ppf (164 MB) runs past sector 65535
    u32 numSectors = entry >> 20;
    u32 pageSize = (numSectors > 0) ? (numSectors * 2048) : 69632;
    if (outPageSize) *outPageSize = pageSize;

    u32 fileOffset = sector * 2048;
    Stream *s = sm_Streams[slot];
    s->Seek((int)fileOffset);

    u32 toRead = (pageSize < bufferSize) ? pageSize : bufferSize;
    return s->Read(outBuffer, (int)toRead) == (int)toRead;
}

bool datPageFile::ReadPageByOffset(int slot, u64 fileOffset, u8 *outBuffer, u32 bufferSize)
{
    if (slot < 0 || slot >= kMaxSlots || !sm_Mounted[slot] || !sm_Streams[slot]) return false;
    if (!outBuffer || bufferSize == 0) return false;
    Stream *s = sm_Streams[slot];
    s->Seek((int)fileOffset);
    return s->Read(outBuffer, (int)bufferSize) == (int)bufferSize;
}

