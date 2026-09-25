////////////////////////////////////////
// hash.h
////////////////////////////////////////

#ifndef DATA_HASH_H
#define DATA_HASH_H
#include "core/output.h"
#include <new>

#include "core/types.h"

struct HashPosition {
  const char *id;
  void *data;
};

class HashTable {
public:
  HashTable();
  HashTable(int size, bool caseSensitive = true, int maxKeyLen = 64,
            bool flag = false);
  HashTable(int size, int maxKeyLen, bool caseSensitive = true);
  // Rebuild empty with `size` buckets.
  void Init(int size) { this->~HashTable(); new (this) HashTable(size); }
  HashTable(class datResource &rsc);
  HashTable(class datResource &rsc, void (*fixup)(class datResource &rsc, void *&ptr));
  ~HashTable();

  static HashTable *Create() { return new HashTable(); }
  static HashTable *Create(int size) { return new HashTable(size); }
  // Kill list.  A table joins it with AddToKillList() and leaves it with
  // MakePermanent() or by being destroyed; KillAll() empties every table still
  // on the list.  (Alpha: HashTable::First at 0x6b66b8, the link at +0x14,
  // AddToKillList / MakePermanent / KillAll / ~HashTable all walk it.)
  void AddToKillList();
  void MakePermanent();
  static void KillAll();

  // The game turns this off at startup so tables do not build their flat slot
  // array ("don't let hash table recompute, so they don't allocate Slot array
  // in various layers", mc.cpp).  Alpha: the static HashTable::s_EnableRecompute
  // that gates Recompute().  This port's table is a fixed 64-bucket chained
  // table -- AccessData/AccessName walk the buckets and it never resizes -- so
  // nothing allocates a slot array to suppress; the flag is stored and readable
  // so a resizing implementation honours the game's intent.
  static void SetEnableRecompute(bool b) { sm_EnableRecompute = b; }
  static bool GetEnableRecompute() { return sm_EnableRecompute; }

  u32 Add(const char *key, void *value);
  void Insert(const char *key, void *value) { Add(key, value); }

  void *Find(const char *key) const;
  void *Access(const char *key, void **data = nullptr) const {
    void *val = Find(key);
    if (data) *data = val;
    return val;
  }
  void *Access(const char *key, const void **data) const {
    void *val = Find(key);
    if (data) *data = val;
    return val;
  }

  void Remove(const char *key);
  void Delete(const char *key) { Remove(key); }
  bool DeleteByData(void *data);
  void Change(const char *oldKey, const char *newName);

  int GetNumEntries() const { return m_Count; }

  // Flat index access over all entries (stable while the table is unmodified).
  void *AccessData(int index) const;
  const char *AccessName(int index) const;

  void GetStartPosition(HashPosition &pos) const;
  bool GetFirstEntry(HashPosition &pos) const {
    GetStartPosition(pos);
    return pos.data != NULL;
  }

  void GetNextPosition(HashPosition &pos) const;
  bool GetNextEntry(HashPosition &pos) const {
    GetNextPosition(pos);
    return pos.data != NULL;
  }

  void *GetValue(HashPosition pos) const;

  void MakeEmpty();
  void Kill() { MakeEmpty(); }

private:
  struct Entry {
    char key[64];
    void *value;
    Entry *next;
  };

  Entry *m_Buckets[64];
  int m_Count;
  HashTable *m_NextToKill;      // kill-list link; NULL when not listed

  static HashTable *sm_FirstToKill;
  static bool sm_EnableRecompute;
};

#endif // DATA_HASH_H
