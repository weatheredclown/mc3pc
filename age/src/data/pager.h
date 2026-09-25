#ifndef DATA_PAGER_H
#define DATA_PAGER_H

////////////////////////////////////////
// data/pager.h
//
// Console texture paging: a page file (.ppf) next to a resource pack holds
// the streamed texture pages, and the page manager owns the RAM pages they
// are read into.  The port reads packs as data (data/rscimage.h) and does
// not stream yet, so the manager only records what the game asks for and
// Mount/Unmount accept every slot.
////////////////////////////////////////

#include "core/output.h"
#include "core/types.h"

class datPager {
public:
    static void InitClass() { Quitf("datPager::InitClass - not implemented"); }
    bool Update() { return false; }
};

extern datPager PAGER;
extern bool datEnablePaging;

class datPageManager {
public:
    datPageManager();
    ~datPageManager();

    static datPageManager &GetInstance();
    static bool IsInitialized() { return sm_Instance != 0; }

    // Reserve `count` pages of `size` bytes (the game sizes them per layer).
    void AddPages(int size, int count);
    void RemovePages(int size, int count);
    // Per-frame streaming step (issue/complete page reads); nothing streams yet.
    void BeginFrameUpdate() { Quitf("datPageManager::BeginFrameUpdate - not implemented"); }
    void EndFrameUpdate() { Quitf("datPageManager::EndFrameUpdate - not implemented"); }
    void Update() { Quitf("datPageManager::Update - not implemented"); }
    int GetNumPages() const { return m_NumPages; }
    int GetPageSize() const { return m_PageSize; }

private:
    static datPageManager *sm_Instance;
    int m_NumPages;
    int m_PageSize;
};

class datPageFile {
public:
    enum { kMaxSlots = 128 };
    // Mounts "<name>.ppf" in a slot; returns false when the file is absent.
    static bool Mount(const char *name, int slot);
    static void Unmount(int slot);
    static bool IsMounted(int slot);
    static const char *GetName(int slot);

    // PPF page file reading
    static int GetNumEntries(int slot);
    static u32 GetDataStart(int slot);
    static u32 GetEntrySector(int slot, int entryIndex);
    static bool ReadEntry(int slot, int entryIndex, u8 *outBuffer, u32 bufferSize, u32 *outPageSize = 0);
    static bool ReadPageByOffset(int slot, u64 fileOffset, u8 *outBuffer, u32 bufferSize);

private:
    static char sm_Names[kMaxSlots][128];
    static bool sm_Mounted[kMaxSlots];
};

#endif // DATA_PAGER_H
