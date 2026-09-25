#include "zipfile/zipfile.h"
#include "zipfile/miniz.h"
#include "core/output.h"
#include <algorithm>

atArray<zipFile*> zipFile::sm_ActiveArchives;

zipFile::zipFile() : m_File(NULL), m_IsZip(false) {
    sm_ActiveArchives.Append(this);
}

zipFile::~zipFile() {
    if (m_File) {
        fclose(m_File);
    }
    for (int i = 0; i < sm_ActiveArchives.GetCount(); ++i) {
        if (sm_ActiveArchives[i] == this) {
            for (int j = i; j < sm_ActiveArchives.GetCount() - 1; ++j) {
                sm_ActiveArchives[j] = sm_ActiveArchives[j + 1];
            }
            sm_ActiveArchives.Resize(sm_ActiveArchives.GetCount() - 1);
            break;
        }
    }
}

bool zipFile::Init(const char *name) {
    m_File = fopen(name, "rb");
    if (!m_File) {
        return false;
    }

    m_ArchivePath = name;

    char magic[4];
    if (fread(magic, 1, 4, m_File) != 4) {
        fclose(m_File);
        m_File = NULL;
        return false;
    }

    if (strncmp(magic, "DAVE", 4) == 0 || strncmp(magic, "Dave", 4) == 0) {
        // --- DAVE Archive Parser ---
        // "DAVE": NUL-terminated names.  "Dave" (the per-car vp_*.dat archives nested in
        // ASSETS.DAT): names are a stream of 6-bit codes read LSB-first from the byte the
        // entry's nameOffset names; codes < 48 index the alphabet below (0 ends the name),
        // a code c >= 48 copies (c - 0x38) + (next - 0x20) * 8 characters from the start
        // of the previous entry's name (aluigi's dave.bms / age/tools/dave_extract.py).
        const bool packedNames = (magic[1] == 'a');
        u32 fileCount = 0;
        u32 dirSize = 0;
        u32 namesSize = 0;
        fread(&fileCount, 4, 1, m_File);
        fread(&dirSize, 4, 1, m_File);
        fread(&namesSize, 4, 1, m_File);

        // Read directory entries block at 0x800
        fseek(m_File, 0x800, SEEK_SET);
        atArray<u8> dirEntries;
        dirEntries.Resize(fileCount * 16);
        if (fread(dirEntries.begin(), 1, fileCount * 16, m_File) != fileCount * 16) {
            fclose(m_File);
            m_File = NULL;
            return false;
        }

        // Read string table block at 0x800 + dirSize
        fseek(m_File, 0x800 + dirSize, SEEK_SET);
        atArray<char> stringTable;
        stringTable.Resize(1024 * 1024 * 4); // 4MB should cover any string table.
        size_t bytesRead = fread(stringTable.begin(), 1, stringTable.GetCount(), m_File);
        stringTable.Resize(bytesRead);

        m_Entries.Reset();

        static const char kDaveAlphabet[] = "\0 #$()-./?0123456789_abcdefghijklmnopqrstuvwxyz~";
        std::string prevName;
        for (u32 i = 0; i < fileCount; ++i) {
            u8 *chunk = &dirEntries[i * 16];
            u32 nameOffset = *(u32*)(&chunk[0]);
            u32 fileOffset = *(u32*)(&chunk[4]);
            u32 uncompressed = *(u32*)(&chunk[8]);
            u32 compressed = *(u32*)(&chunk[12]);

            if (nameOffset < (u32)stringTable.GetCount()) {
                std::string decoded;
                const char *rawName = &stringTable[nameOffset];
                if (packedNames) {
                    const u8 *bits = (const u8 *)stringTable.begin();
                    u32 bitPos = nameOffset * 8;
                    u32 bitEnd = (u32)stringTable.GetCount() * 8;
                    for (;;) {
                        if (bitPos + 6 > bitEnd) break;
                        u32 code = 0;
                        for (int b = 0; b < 6; b++, bitPos++)
                            code |= (u32)((bits[bitPos >> 3] >> (bitPos & 7)) & 1) << b;
                        if (code >= 48) {
                            if (bitPos + 6 > bitEnd) break;
                            u32 next = 0;
                            for (int b = 0; b < 6; b++, bitPos++)
                                next |= (u32)((bits[bitPos >> 3] >> (bitPos & 7)) & 1) << b;
                            size_t len = (size_t)(code - 0x38) + (size_t)(next - 0x20) * 8;
                            decoded += prevName.substr(0, std::min(len, prevName.size()));
                            continue;
                        }
                        char ch = kDaveAlphabet[code];
                        if (ch == 0) break;
                        decoded += ch;
                    }
                    rawName = decoded.c_str();
                    prevName = decoded;
                }
                Entry entry;
                entry.Name = rawName;
                std::replace(entry.Name.begin(), entry.Name.end(), '\\', '/');
                entry.FileOffset = fileOffset;
                entry.UncompressedSize = uncompressed;
                entry.CompressedSize = compressed;
                m_Entries.Append(entry);
            }
        }
        Displayf("Mounted DAVE archive '%s' with %d files.", name, m_Entries.GetCount());
        return true;
    } 
    else if (magic[0] == 'P' && magic[1] == 'K' && magic[2] == 0x03 && magic[3] == 0x04) {
        // --- ZIP Archive Parser ---
        fseek(m_File, 0, SEEK_END);
        long fileSize = ftell(m_File);
        long searchStart = std::max(0L, fileSize - 65535 - 22);
        fseek(m_File, searchStart, SEEK_SET);
        
        atArray<u8> buffer;
        buffer.Resize(fileSize - searchStart);
        if (fread(buffer.begin(), 1, buffer.GetCount(), m_File) == 0) {
            fclose(m_File);
            m_File = NULL;
            return false;
        }
        
        long eocdOffset = -1;
        for (long i = (long)buffer.GetCount() - 22; i >= 0; --i) {
            if (buffer[i] == 0x50 && buffer[i+1] == 0x4b && buffer[i+2] == 0x05 && buffer[i+3] == 0x06) {
                eocdOffset = searchStart + i;
                break;
            }
        }
        
        if (eocdOffset == -1) {
            fclose(m_File);
            m_File = NULL;
            return false;
        }
        
        u8 *eocd = &buffer[eocdOffset - searchStart];
        u16 numEntries = *(u16*)(&eocd[10]);
        u32 cdSize = *(u32*)(&eocd[12]);
        u32 cdOffset = *(u32*)(&eocd[16]);
        
        fseek(m_File, cdOffset, SEEK_SET);
        m_Entries.Reset();

        m_IsZip = true;
        for (u16 i = 0; i < numEntries; ++i) {
            char cdMagic[4];
            if (fread(cdMagic, 1, 4, m_File) != 4 || cdMagic[0] != 'P' || cdMagic[1] != 'K' || cdMagic[2] != 0x01 || cdMagic[3] != 0x02) {
                break;
            }
            
            fseek(m_File, 16, SEEK_CUR);
            u32 compressed = 0;
            u32 uncompressed = 0;
            fread(&compressed, 4, 1, m_File);
            fread(&uncompressed, 4, 1, m_File);
            
            u16 filenameLen = 0;
            u16 extraLen = 0;
            u16 commentLen = 0;
            fread(&filenameLen, 2, 1, m_File);
            fread(&extraLen, 2, 1, m_File);
            fread(&commentLen, 2, 1, m_File);
            
            fseek(m_File, 8, SEEK_CUR);
            u32 localHeaderOffset = 0;
            fread(&localHeaderOffset, 4, 1, m_File);
            
            atArray<char> filenameBuf;
            filenameBuf.Resize(filenameLen + 1);
            memset(filenameBuf.begin(), 0, filenameLen + 1);
            fread(filenameBuf.begin(), 1, filenameLen, m_File);
            
            fseek(m_File, extraLen + commentLen, SEEK_CUR);
            
            Entry entry;
            entry.Name = filenameBuf.begin();
            std::replace(entry.Name.begin(), entry.Name.end(), '\\', '/');
            entry.FileOffset = localHeaderOffset;
            entry.UncompressedSize = uncompressed;
            entry.CompressedSize = compressed;
            
            if (!entry.Name.empty() && entry.Name.back() != '/') {
                m_Entries.Append(entry);
            }
        }
        Displayf("Mounted ZIP archive '%s' with %d files.", name, m_Entries.GetCount());
        return true;
    }

    fclose(m_File);
    m_File = NULL;
    return false;
}

const zipFile::Entry* zipFile::FindEntry(const char *filename) const {
    std::string normName = filename;
    std::replace(normName.begin(), normName.end(), '\\', '/');
    // Case-insensitive lookup
    for (const auto &entry : m_Entries) {
        if (_stricmp(entry.Name.c_str(), normName.c_str()) == 0) {
            return &entry;
        }
        if (normName.length() > entry.Name.length() &&
            normName[normName.length() - entry.Name.length() - 1] == '/' &&
            _stricmp(normName.c_str() + (normName.length() - entry.Name.length()), entry.Name.c_str()) == 0) {
            return &entry;
        }
    }
    return NULL;
}

Stream* zipFile::OpenStream(const Entry *entry) {
    if (!m_File) return NULL;
    FILE *dupFile = fopen(m_ArchivePath.c_str(), "rb");
    if (!dupFile) return NULL;

    u32 fileOffset = entry->FileOffset;
    if (m_IsZip) {
        // Read local header to get filename and extra field lengths
        fseek(dupFile, entry->FileOffset + 26, SEEK_SET);
        u16 localFilenameLen = 0;
        u16 localExtraLen = 0;
        fread(&localFilenameLen, 2, 1, dupFile);
        fread(&localExtraLen, 2, 1, dupFile);
        fileOffset = entry->FileOffset + 30 + localFilenameLen + localExtraLen;
    }

    return new zipStream(dupFile, fileOffset, entry->UncompressedSize, entry->CompressedSize, entry->Name.c_str());
}

zipFile *zipFile::Mount(const char *path) {
    if (!path || !path[0]) return NULL;
    zipFile *z = new zipFile;
    if (!z->Init(path)) {
        delete z;
        return NULL;
    }
    return z;
}

void zipFile::Unmount(zipFile *archive) {
    delete archive;
}

void zipFile::KillAll() {
    while (!sm_ActiveArchives.IsEmpty()) {
        delete sm_ActiveArchives.Last();
    }
}

Stream* zipFile::OpenFromArchives(const char *filename) {
    for (auto *archive : sm_ActiveArchives) {
        const auto *entry = archive->FindEntry(filename);
        if (entry) {
            return archive->OpenStream(entry);
        }
    }
    return NULL;
}

bool zipFile::ExistsInArchives(const char *filename) {
    for (auto *archive : sm_ActiveArchives) {
        if (archive->FindEntry(filename)) {
            return true;
        }
    }
    return false;
}

// zipStream implementation
zipStream::zipStream(FILE *archiveFile, u32 fileOffset, u32 uncompressedSize, u32 compressedSize, const char *name)
    : m_ArchiveFile(archiveFile), m_StartOffset(fileOffset), m_CurrentOffset(0),
      m_UncompressedSize(uncompressedSize), m_CompressedSize(compressedSize),
      m_DecompressedData(NULL), m_BufferPos(0) {
    strncpy(m_Name, name ? name : "", sizeof(m_Name) - 1);
    m_Name[sizeof(m_Name) - 1] = '\0';
}

zipStream::~zipStream() {
    if (m_ArchiveFile) {
        fclose(m_ArchiveFile);
    }
    if (m_DecompressedData) {
        delete[] m_DecompressedData;
    }
}

void zipStream::Close() {
    delete this;
}

int zipStream::Read(void *pData, int nSize) {
    if (!m_ArchiveFile) return 0;

    if (m_CompressedSize != 0 && m_CompressedSize != m_UncompressedSize) {
        if (!m_DecompressedData) {
            m_DecompressedData = new u8[m_UncompressedSize];
            fseek(m_ArchiveFile, m_StartOffset, SEEK_SET);
            atArray<u8> compressedData;
            compressedData.Resize(m_CompressedSize);
            fread(compressedData.begin(), 1, m_CompressedSize, m_ArchiveFile);
            
            // DAVE and ZIP entries both store raw deflate (no zlib header),
            // so inflate with negative window bits. Some DAVE entries prefix
            // the deflate stream with a 4-byte 1A FA 25 DD wrapper -- skip it
            // (see Oni2Rebuilt's dave_vfs.rs, which handles the same format).
            const u8 *src = compressedData.begin();
            u32 srcLen = m_CompressedSize;
            if (srcLen >= 4 && src[0] == 0x1A && src[1] == 0xFA && src[2] == 0x25 && src[3] == 0xDD) {
                src += 4;
                srcLen -= 4;
            }
            mz_stream strm = {};
            int status = mz_inflateInit2(&strm, -MZ_DEFAULT_WINDOW_BITS);
            if (status == MZ_OK) {
                strm.next_in = src;
                strm.avail_in = srcLen;
                strm.next_out = m_DecompressedData;
                strm.avail_out = m_UncompressedSize;
                status = mz_inflate(&strm, MZ_FINISH);
                mz_inflateEnd(&strm);
            }
            if (status != MZ_STREAM_END) {
                Errorf("Failed to decompress '%s' from archive: error %d", m_Name, status);
            }
        }
        
        int bytesToRead = std::min((u32)nSize, m_UncompressedSize - m_CurrentOffset);
        if (bytesToRead > 0) {
            memcpy(pData, m_DecompressedData + m_CurrentOffset, bytesToRead);
            m_CurrentOffset += bytesToRead;
        }
        return bytesToRead;
    } else {
        fseek(m_ArchiveFile, m_StartOffset + m_CurrentOffset, SEEK_SET);
        int bytesToRead = std::min((u32)nSize, m_UncompressedSize - m_CurrentOffset);
        int bytesRead = fread(pData, 1, bytesToRead, m_ArchiveFile);
        m_CurrentOffset += bytesRead;
        return bytesRead;
    }
}

int zipStream::Write(const void *pData, int nSize) {
    return 0;
}

void zipStream::Flush() {}

int zipStream::GetCh() {
    u8 c = 0;
    if (Read(&c, 1) == 1) return c;
    return -1;
}

int zipStream::FastGetCh() {
    return GetCh();
}

int zipStream::Tell() {
    return m_CurrentOffset;
}

void zipStream::Seek(int offset) {
    m_CurrentOffset = std::min((u32)offset, m_UncompressedSize);
}

int zipStream::Size() {
    return m_UncompressedSize;
}

int zipStream::FastPutCh(int c) {
    return -1;
}

void zipStream::PreLoad(int size) {}
