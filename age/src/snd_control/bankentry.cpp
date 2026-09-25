////////////////////////////////////////
// bankentry.cpp
////////////////////////////////////////

#include "snd_control/bankentry.h"
#include <string.h>

CBankEntry::CBankEntry() {
    m_Name[0] = '\0';
}

CBankEntry::CBankEntry(const char *name) {
    strncpy(m_Name, name ? name : "", sizeof(m_Name) - 1);
    m_Name[sizeof(m_Name) - 1] = '\0';
}

CBankEntry::~CBankEntry() {
}

sndBankSound::sndBankSound() : CBankEntry() {
}

sndBankSound::sndBankSound(const char *name) : CBankEntry(name) {
}
