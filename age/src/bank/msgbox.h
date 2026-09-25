// __BANK=0: the bank (RAG debug widget) UI is not part of the build.  The
// whole module is compiled out here rather than left as inert stubs, so any
// code that still reaches for a widget fails loudly at compile time instead
// of silently building a UI that can never be shown.
#if __BANK

#ifndef BANK_MSGBOX_H
#define BANK_MSGBOX_H

// Native message box.  bkMessageBox returns 1 for OK/Yes, 0 for No.
enum {
    bkMsgOk = 0,
    bkMsgYesNo = 1
};
enum {
    bkMsgDefault = 0,
    bkMsgInfo,
    bkMsgQuestion,
    bkMsgError
};
enum {
    bkmbDefault = 0
};

int bkMessageBox(const char *title, const char *message, int type = bkMsgOk, int icon = bkMsgDefault);
void bkMessageBeep(int kind);

#endif // BANK_MSGBOX_H

#endif // __BANK
