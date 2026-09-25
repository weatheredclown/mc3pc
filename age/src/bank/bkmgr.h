// __BANK=0: the bank (RAG debug widget) UI is not part of the build.  The
// whole module is compiled out here rather than left as inert stubs, so any
// code that still reaches for a widget fails loudly at compile time instead
// of silently building a UI that can never be shown.
#if __BANK

#ifndef BANK_BKMGR_H
#define BANK_BKMGR_H

#include "core/output.h"
#include "bank/bank.h"
#include "atl/array.h"

class bkBankManager {
public:
    bkBankManager();
    ~bkBankManager();

    bkBank& CreateBank(const char* name, int x = 0, int y = 0);
    bkBank& CreateBank(const char* name, int x, int y, bool flag);
    bkBank* FindBank(const char* name);
    void DestroyBank(bkBank &bank);
    void ActivateBank(const char* name);
    void SetActive(bkBank *bank) { if (bank) ActivateBank(bank->GetName()); }
    void SetActive(const char *name) { ActivateBank(name); }
    void SetActive(bool active) { (void)active; }
    // Put up the platform file dialog and return the chosen path, or NULL if
    // the user cancelled.  `filter` is a bare pattern ("*.bmp"), `title` the
    // label shown for it, and `dirKey` names a slot that remembers the folder
    // last picked through it, so a tool comes back where it left off.  The
    // returned pointer is a shared static buffer: use it before calling again.
    const char* OpenFile(const char* filter = nullptr, bool save = false, const char* title = nullptr,
                         const char* dirKey = nullptr);
    void Draw();
    void Update();
    void SetFullscreen(bool fullscreen);

    bool IsUsingPad() const { return false; }
    bool IsUsingPad(int pad) const { (void)pad; return false; }
    // Move the master panel; every other bank cascades from it (CreateBank).
    void MoveMainBank(int x, int y);
    // Create `name` and let `addWidgets` populate it (called with the bank).
    bkBank &RegisterBank(const char *name, const class datCallback &addWidgets);

private:
    void EnsureMainBank();

    atArray<bkBank*> m_Banks;
    bkBank* m_MainBank;   // always-visible master panel of per-bank toggle buttons
};

class bkManager {
public:
    static void CreateBankManager(const char* name = nullptr);
    static void CreateBankManager(void* callback);
    static void DeleteBankManager();
    static bool IsEnabled();
    static bkBankManager* GetInstance();
};

#define BANKMGR (*bkManager::GetInstance())

class bkRemotePacket {
public:
    static void Connect() { Quitf("bkRemotePacket::Connect - not implemented"); }
};

// Screen origin of the bank windows.  The game moves them off the play area
// (mcCarAudio uses 8,8) and offers the position as sliders in its own bank, so
// the base is real state: it places the master panel, and every other bank
// cascades from that one.
class bkIo {
public:
    static bkIo* GetInstance();
    static void SetBase(int x, int y);
    static int GetBaseX() { return sm_BaseX; }
    static int GetBaseY() { return sm_BaseY; }
    void AddWidgets(class bkBank &bank);
    static int sm_BaseX;
    static int sm_BaseY;
};

#endif // BANK_BKMGR_H

#endif // __BANK
