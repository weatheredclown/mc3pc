#include "rmcore/shadertemplate.h"
#include "data/assetcfg.h"
#include "core/stream.h"
#include "core/output.h"

#include <cstring>
#include <cstdlib>

rmcShaderTemplateCache rmcShadertCache;

rmcShaderTemplateCache::rmcShaderTemplateCache() : m_Head(nullptr), m_Count(0) {}

rmcShaderTemplateCache::~rmcShaderTemplateCache() {
    Kill();
}

rmcShaderTemplateCache::Entry *rmcShaderTemplateCache::Find(const char *name) const {
    if (!name) return nullptr;
    for (Entry *e = m_Head; e; e = e->Next) {
        if (_stricmp(e->Name, name) == 0) return e;
    }
    return nullptr;
}

bool rmcShaderTemplateCache::Contains(const char *name) const {
    return Find(name) != nullptr;
}

const char *rmcShaderTemplateCache::Get(const char *name) {
    if (!name || !*name) return nullptr;
    if (Entry *e = Find(name)) return e->Text;

    // Strip a trailing ".shadert" so both spellings hit the same entry.
    char base[256];
    strncpy(base, name, sizeof(base) - 1); base[sizeof(base) - 1] = 0;
    size_t n = strlen(base);
    if (n > 8 && _stricmp(base + n - 8, ".shadert") == 0) base[n - 8] = 0;
    if (Entry *e = Find(base)) return e->Text;

    Stream *s = ASSET.Exists(base, "shadert") ? ASSET.Open(base, "shadert") : nullptr;
    if (!s) {
        char sub[256];
        snprintf(sub, sizeof(sub), "shaderlib/city/%s", base);
        if (ASSET.Exists(sub, "shadert")) s = ASSET.Open(sub, "shadert");
    }
    if (!s) {
        char sub[256];
        snprintf(sub, sizeof(sub), "shaderlib/%s", base);
        if (ASSET.Exists(sub, "shadert")) s = ASSET.Open(sub, "shadert");
    }
    if (!s) return nullptr;

    // Slurp the file; templates are small text files.
    size_t cap = 4096, len = 0;
    char *text = (char *)malloc(cap);
    char buf[1024];
    int r;
    while (text && (r = s->Read(buf, sizeof(buf))) > 0) {
        if (len + (size_t)r + 1 > cap) {
            cap = (len + (size_t)r + 1) * 2;
            char *grown = (char *)realloc(text, cap);
            if (!grown) { free(text); text = nullptr; break; }
            text = grown;
        }
        memcpy(text + len, buf, (size_t)r);
        len += (size_t)r;
    }
    s->Close();
    if (!text) return nullptr;
    text[len] = 0;

    Entry *e = new Entry;
    e->Name = _strdup(base);
    e->Text = text;
    e->Next = m_Head;
    m_Head = e;
    ++m_Count;
    return e->Text;
}

void rmcShaderTemplateCache::Kill() {
    Entry *e = m_Head;
    while (e) {
        Entry *next = e->Next;
        free(e->Name);
        free(e->Text);
        delete e;
        e = next;
    }
    m_Head = nullptr;
    m_Count = 0;
}
