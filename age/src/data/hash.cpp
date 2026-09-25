////////////////////////////////////////
// hash.cpp
////////////////////////////////////////

#include "data/hash.h"
#include <string.h>

#define OLD_HASH 0

u32 HashString(const char *str) {
#if OLD_HASH
  u32 hash = 5381;
  int c;
  while ((c = *str++)) {
    hash = ((hash << 5) + hash) + c;
  }
  return hash;
#else
  u32 hash = 0;
  while (*str) {
    signed char c = (signed char)*str++;
    hash = (hash << 4) + c;
    u32 mask = hash & 0xF0000000;
    if (mask) {
      hash ^= (mask >> 24) ^ mask;
    }
  }
  return hash;
#endif
}

HashTable *HashTable::sm_FirstToKill = NULL;
bool HashTable::sm_EnableRecompute = true;

HashTable::HashTable() {
  m_Count = 0;
  m_NextToKill = NULL;
  memset(m_Buckets, 0, sizeof(m_Buckets));
}

HashTable::HashTable(int size, bool caseSensitive, int maxKeyLen, bool flag) {
  m_Count = 0;
  m_NextToKill = NULL;
  memset(m_Buckets, 0, sizeof(m_Buckets));
}

HashTable::~HashTable() {
  MakePermanent();          // leave the kill list before the memory goes away
  for (int i = 0; i < 64; i++) {
    Entry *entry = m_Buckets[i];
    while (entry) {
      Entry *next = entry->next;
      delete entry;
      entry = next;
    }
  }
}

u32 HashTable::Add(const char *key, void *value) {
  u32 hash = HashString(key);
  u32 bucket = hash % 64;
  Entry *entry = new Entry();
  strncpy(entry->key, key, sizeof(entry->key) - 1);
  entry->key[sizeof(entry->key) - 1] = '\0';
  entry->value = value;
  entry->next = m_Buckets[bucket];
  m_Buckets[bucket] = entry;
  m_Count++;
  return hash;
}

void *HashTable::Find(const char *key) const {
  u32 bucket = HashString(key) % 64;
  Entry *entry = m_Buckets[bucket];
  while (entry) {
    if (strcmp(entry->key, key) == 0) {
      return entry->value;
    }
    entry = entry->next;
  }
  return NULL;
}

void HashTable::Remove(const char *key) {
  u32 bucket = HashString(key) % 64;
  Entry *entry = m_Buckets[bucket];
  Entry *prev = NULL;
  while (entry) {
    if (strcmp(entry->key, key) == 0) {
      if (prev) {
        prev->next = entry->next;
      } else {
        m_Buckets[bucket] = entry->next;
      }
      delete entry;
      m_Count--;
      break;
    }
    prev = entry;
    entry = entry->next;
  }
}

void HashTable::GetStartPosition(HashPosition &pos) const {
  for (int i = 0; i < 64; i++) {
    if (m_Buckets[i]) {
      pos.id = m_Buckets[i]->key;
      pos.data = m_Buckets[i]->value;
      return;
    }
  }
  pos.id = NULL;
  pos.data = NULL;
}

void HashTable::GetNextPosition(HashPosition &pos) const {
  if (!pos.data)
    return;
  for (int i = 0; i < 64; i++) {
    Entry *entry = m_Buckets[i];
    while (entry) {
      if (entry->value == pos.data) {
        if (entry->next) {
          pos.id = entry->next->key;
          pos.data = entry->next->value;
          return;
        }
        for (int j = i + 1; j < 64; j++) {
          if (m_Buckets[j]) {
            pos.id = m_Buckets[j]->key;
            pos.data = m_Buckets[j]->value;
            return;
          }
        }
        pos.id = NULL;
        pos.data = NULL;
        return;
      }
      entry = entry->next;
    }
  }
  pos.id = NULL;
  pos.data = NULL;
}

void *HashTable::GetValue(HashPosition pos) const { return pos.data; }

void *HashTable::AccessData(int index) const {
  int seen = 0;
  for (int i = 0; i < 64; i++) {
    for (Entry *entry = m_Buckets[i]; entry; entry = entry->next) {
      if (seen == index)
        return entry->value;
      seen++;
    }
  }
  return NULL;
}

const char *HashTable::AccessName(int index) const {
  int seen = 0;
  for (int i = 0; i < 64; i++) {
    for (Entry *entry = m_Buckets[i]; entry; entry = entry->next) {
      if (seen == index)
        return entry->key;
      seen++;
    }
  }
  return NULL;
}

void HashTable::MakeEmpty() {
  for (int i = 0; i < 64; i++) {
    Entry *entry = m_Buckets[i];
    while (entry) {
      Entry *next = entry->next;
      delete entry;
      entry = next;
    }
    m_Buckets[i] = NULL;
  }
  m_Count = 0;
}

void HashTable::Change(const char *oldKey, const char *newName) {
  void *val = Find(oldKey);
  if (val) {
    Remove(oldKey);
    Add(newName, val);
  }
}

bool HashTable::DeleteByData(void *data) {
  bool deleted = false;
  for (int i = 0; i < 64; i++) {
    Entry *prev = nullptr;
    Entry *curr = m_Buckets[i];
    while (curr) {
      if (curr->value == data) {
        Entry *next = curr->next;
        delete curr;
        if (prev) {
          prev->next = next;
        } else {
          m_Buckets[i] = next;
        }
        m_Count--;
        curr = next;
        deleted = true;
      } else {
        prev = curr;
        curr = curr->next;
      }
    }
  }
  return deleted;
}

// Resource-built tables start empty (the resource fixup re-adds entries).
HashTable::HashTable(datResource & /*rsc*/) {
  m_Count = 0;
  memset(m_Buckets, 0, sizeof(m_Buckets));
}

HashTable::HashTable(datResource & /*rsc*/, void (* /*fixup*/)(datResource &, void *&)) {
  m_Count = 0;
  memset(m_Buckets, 0, sizeof(m_Buckets));
}

////////////////////////////////////////
// Kill list
//
// A table on this list is emptied by KillAll() -- the game uses it to drop
// every non-permanent table when a heap layer is torn down.  Joining is
// idempotent, and a table must leave the list before it is destroyed or
// KillAll() would walk freed memory.
////////////////////////////////////////

void HashTable::AddToKillList() {
  if (m_NextToKill) return;                 // already listed
  if (sm_FirstToKill == this) return;       // listed as the (unlinked) head
  m_NextToKill = sm_FirstToKill;
  sm_FirstToKill = this;
}

void HashTable::MakePermanent() {
  if (sm_FirstToKill == this) {
    sm_FirstToKill = m_NextToKill;
    m_NextToKill = NULL;
    return;
  }
  for (HashTable *prev = sm_FirstToKill; prev; prev = prev->m_NextToKill) {
    if (prev->m_NextToKill == this) {
      prev->m_NextToKill = m_NextToKill;
      m_NextToKill = NULL;
      return;
    }
  }
}

void HashTable::KillAll() {
  for (HashTable *t = sm_FirstToKill; t; t = t->m_NextToKill)
    t->MakeEmpty();
}
