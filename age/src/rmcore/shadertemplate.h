#ifndef RMCORE_SHADERTEMPLATE_H
#define RMCORE_SHADERTEMPLATE_H

////////////////////////////////////////
// rmcore/shadertemplate.h
//
// Cache of parsed .shadert template files.  Loading a level's shader type
// file instantiates hundreds of shaders from a few dozen templates; the
// cache keeps each template's text in memory for the duration of that load
// so it is read from the archive once.  The game calls rmcShadertCache.Kill()
// when it has finished creating shaders.
////////////////////////////////////////

class rmcShaderTemplateCache {
public:
    rmcShaderTemplateCache();
    ~rmcShaderTemplateCache();

    // Text of "<name>.shadert" (loaded through ASSET on first use), or NULL
    // if the file does not exist.  The returned buffer is owned by the cache
    // and stays valid until Kill().
    const char *Get(const char *name);
    bool Contains(const char *name) const;
    int GetCount() const { return m_Count; }

    // Frees every cached template.
    void Kill();

private:
    struct Entry {
        char *Name;
        char *Text;
        Entry *Next;
    };
    Entry *Find(const char *name) const;

    Entry *m_Head;
    int m_Count;
};

extern rmcShaderTemplateCache rmcShadertCache;

#endif // RMCORE_SHADERTEMPLATE_H
