////////////////////////////////////////
// materialmgr.h
//
// phMaterialMgr class definitions.
////////////////////////////////////////

#ifndef PHCORE_MATERIALMGR_H
#define PHCORE_MATERIALMGR_H

#include "phcore/material.h"
#include "data/hash.h"
#include "atl/array.h"

class phMaterialMgr {
protected:
    atArray<phMaterial*> m_Materials;
public:
    static phMaterialMgr *sm_Instance;
    static phMaterialMgr *Instance;
    static class HashTable Table;

    phMaterialMgr(int a, int maxMaterials);
    virtual ~phMaterialMgr();

    static phMaterialMgr & GetInstance() { return *sm_Instance; }
    static void CreateInstance(const char *name, int maxMaterials);
    static void DeleteInstance();

    phMaterial * GetMaterial(int i);
    virtual phMaterial * Load(datAsciiTokenizer *token);
    int GetNumMaterials() const { return m_Materials.GetCount(); }

    // Game-created materials (vehicle body / traffic materials).  The manager
    // never owns these; `unique` rejects a second material of the same name.
    bool AddMaterial(phMaterial *m, bool unique = false);
    void RemoveMaterial(phMaterial *m);
    phMaterial *FindMaterial(const char *name) const;
};

#define MATERIALMGR (phMaterialMgr::GetInstance())

#endif // PHCORE_MATERIALMGR_H
