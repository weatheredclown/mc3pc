#ifndef ZIPFILE_ZIPFILE_H
#define ZIPFILE_ZIPFILE_H

#include "core/stream.h"
#include "atl/array.h"
#include <string>
#include <stdio.h>

class zipStream : public Stream {
public:
    zipStream(FILE *archiveFile, u32 fileOffset, u32 uncompressedSize, u32 compressedSize, const char *name);
    virtual ~zipStream();

    virtual const char* GetName() const override { return m_Name; }
    virtual void Close() override;
    virtual int Read(void *pData, int nSize) override;
    virtual int Write(const void *pData, int nSize) override;
    virtual void Flush() override;
    virtual int GetCh() override;
    virtual int FastGetCh() override;
    virtual int Tell() override;
    virtual void Seek(int offset) override;
    virtual int Size() override;
    virtual int FastPutCh(int c) override;
    virtual void PreLoad(int size) override;

private:
    FILE *m_ArchiveFile;
    u32 m_StartOffset;
    u32 m_CurrentOffset;
    u32 m_UncompressedSize;
    u32 m_CompressedSize;
    
    u8 *m_DecompressedData;
    u32 m_BufferPos;
    char m_Name[256];
};

class zipFile {
public:
    struct Entry {
        std::string Name;
        u32 FileOffset;
        u32 UncompressedSize;
        u32 CompressedSize;
    };

    zipFile();
    ~zipFile();

    bool Init(const char *name);
    const Entry* FindEntry(const char *filename) const;
    Stream* OpenStream(const Entry *entry);

    static void KillAll();
    // Mounts one archive (DAVE / Dave / zip) into the lookup list; NULL when it cannot be
    // opened.  Unmount removes and closes it (the destructor drops it from the list).
    static zipFile *Mount(const char *path);
    static void Unmount(zipFile *archive);
    const char *GetPath() const { return m_ArchivePath.c_str(); }
    int GetNumEntries() const { return m_Entries.GetCount(); }
    static Stream* OpenFromArchives(const char *filename);
    static bool ExistsInArchives(const char *filename);

private:
    FILE *m_File;
    atArray<Entry> m_Entries;
    std::string m_ArchivePath;
    bool m_IsZip;

    static atArray<zipFile*> sm_ActiveArchives;
};

#endif // ZIPFILE_ZIPFILE_H
