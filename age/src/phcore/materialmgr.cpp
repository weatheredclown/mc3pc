////////////////////////////////////////
// materialmgr.cpp
//
// phMaterialMgr implementation.
////////////////////////////////////////

#include "phcore/materialmgr.h"
#include "core/stream.h"
#include "data/token.h"

phMaterialMgr * phMaterialMgr::sm_Instance = nullptr;
phMaterialMgr * phMaterialMgr::Instance = nullptr;
HashTable phMaterialMgr::Table;

phMaterialMgr::phMaterialMgr(int a, int maxMaterials)
{
    sm_Instance = this;
    Instance = this;
}

phMaterialMgr::~phMaterialMgr()
{
    if (sm_Instance == this) {
        sm_Instance = nullptr;
    }
    if (Instance == this) {
        Instance = nullptr;
    }
    for (auto m : m_Materials) {
        delete m;
    }
}

void phMaterialMgr::CreateInstance(const char *name, int maxMaterials)
{
    if (!Instance) {
        new phMaterialMgr(0, maxMaterials);
    }
    if (name && Instance) {
        Stream *stream = Stream::Open(name);
        if (stream) {
            datAsciiTokenizer token;
            token.Init(name, stream);
            
            if (token.CheckToken("version:")) {
                token.GetFloat();
            }
            
            if (token.CheckToken("materials:")) {
                int count = token.GetInt();
                Instance->m_Materials.Resize(count);
                for (int i = 0; i < count; ++i) {
                    Instance->Load(&token);
                }
            }
            stream->Close();
        }
    }
}

void phMaterialMgr::DeleteInstance()
{
    if (Instance) {
        delete Instance;
    }
}

phMaterial * phMaterialMgr::Load(datAsciiTokenizer *token)
{
    phMaterial *pm = new phMaterial();
    if (token) {
        pm->Load(*token);
    }
    
    int idx = -1;
    for (int i = 0; i < m_Materials.GetCount(); ++i) {
        if (m_Materials[i] == nullptr) {
            idx = i;
            break;
        }
    }
    if (idx != -1) {
        m_Materials[idx] = pm;
    } else {
        m_Materials.Append(pm);
    }
    return pm;
}

phMaterial * phMaterialMgr::GetMaterial(int i)
{
    if (i >= 0 && i < m_Materials.GetCount()) {
        return m_Materials[i];
    }
    return nullptr;
}

#include <string.h>

phMaterial *phMaterialMgr::FindMaterial(const char *name) const
{
    if (!name) return nullptr;
    for (int i = 0; i < m_Materials.GetCount(); i++)
        if (m_Materials[i] && strcmp(m_Materials[i]->GetName(), name) == 0) return m_Materials[i];
    return nullptr;
}

bool phMaterialMgr::AddMaterial(phMaterial *m, bool unique)
{
    if (!m) return false;
    if (unique && FindMaterial(m->GetName())) return false;
    for (int i = 0; i < m_Materials.GetCount(); i++)
        if (m_Materials[i] == m) return true;
    m_Materials.Append(m);
    return true;
}

void phMaterialMgr::RemoveMaterial(phMaterial *m)
{
    for (int i = 0; i < m_Materials.GetCount(); i++) {
        if (m_Materials[i] == m) { m_Materials.Delete(i); return; }
    }
}
