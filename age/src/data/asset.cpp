#include "core/stream.h"
#include "core/file.h"
#include <windows.h>
#include "data/assetcfg.h"
#include "zipfile/zipfile.h"
#include <stdio.h>
#include <string.h>

void (*datAssetManager::sm_OpenHook)() = nullptr;
datAssetManager *datAssetManager::sm_Instance = NULL;

class MemoryStream : public Stream {
public:
  MemoryStream(const char *data, const char *name) : m_Data(data), m_Offset(0) {
    m_Length = (int)strlen(data);
    strncpy(m_Name, name, sizeof(m_Name) - 1);
    m_Name[sizeof(m_Name) - 1] = '\0';
  }
  virtual const char *GetName() const override { return m_Name; }
  virtual void Close() override { delete this; }
  virtual int Read(void *pData, int nSize) override {
    if (m_Offset >= m_Length)
      return 0;
    int to_read = nSize;
    if (m_Offset + to_read > m_Length) {
      to_read = m_Length - m_Offset;
    }
    memcpy(pData, m_Data + m_Offset, to_read);
    m_Offset += to_read;
    return to_read;
  }
  virtual int GetCh() override {
    if (m_Offset >= m_Length)
      return -1;
    return (unsigned char)m_Data[m_Offset++];
  }
  virtual int FastGetCh() override {
    if (m_Offset >= m_Length)
      return -1;
    return (unsigned char)m_Data[m_Offset++];
  }
  virtual int Tell() override { return m_Offset; }
  virtual void Seek(int offset) override {
    m_Offset = offset;
    if (m_Offset < 0)
      m_Offset = 0;
    if (m_Offset > m_Length)
      m_Offset = m_Length;
  }

private:
  const char *m_Data;
  int m_Length;
  int m_Offset;
  char m_Name[256];
};

Stream *datAssetManager::Open(const char *base, const char *ext) {
  if (sm_OpenHook) {
    sm_OpenHook();
  }
  char fullPath[512];

  // Pushed extensions are tried first: base.<pushed>.ext (innermost first).
  for (int i = m_ExtensionDepth - 1; i >= 0; i--) {
    char withExt[512];
    FullPath(withExt, sizeof(withExt), base, m_Extensions[i]);
    if (ext && ext[0]) {
      strcat(withExt, ".");
      strcat(withExt, ext);
    }
    Stream *s = Stream::Open(withExt);
    if (s) return s;
  }

  FullPath(fullPath, sizeof(fullPath), base, ext);
  // printf("[ASSET OPEN] Base = %s, Ext = %s, FullPath = %s\n",
  //        base ? base : "NULL", ext ? ext : "NULL", fullPath);
  // fflush(stdout);

  Stream *s = Stream::Open(fullPath);
  if (s) {
    return s;
  }

  // Do NOT log here. Open() is a low-level probe: callers routinely try an
  // optional file (a high-res texture, a shader override, an alternate model
  // extension) and fall back when it is absent. Logging every miss turns normal
  // fallback into a wall of "FAIL" noise. The loader that knows a file is
  // *required* is responsible for erroring when nothing could be loaded.

  if (ext && (strstr(base, "level") != NULL || strstr(base, "Level") != NULL)) {
    if (_stricmp(ext, "shader") == 0) {
      printf("Returning MemoryStream for shader\n");
      return new MemoryStream("", "level.shader");
    } else if (_stricmp(ext, "world") == 0) {
      printf("Returning MemoryStream for world\n");
      return new MemoryStream("LEVEL {\n}", "level.world");
    }
  }

  return NULL;
}

static int sFolderNameCompare(const void *a, const void *b) {
  return _stricmp((const char *)a, (const char *)b);
}

int datAssetManager::ListFolders(const char *folder, char *names, int stride,
                                 int maxCount) {
  char pattern[512];
  FullPath(pattern, sizeof(pattern) - 3, folder, "");
  size_t n = strlen(pattern);
  // FullPath appends the (empty) extension dot handling; ensure "/*"
  while (n && (pattern[n - 1] == '.' || pattern[n - 1] == '/'))
    pattern[--n] = 0;
  strcat(pattern, "/*");

  WIN32_FIND_DATAA fd;
  HANDLE h = FindFirstFileA(pattern, &fd);
  int count = 0;
  if (h != INVALID_HANDLE_VALUE) {
    do {
      if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        continue;
      if (fd.cFileName[0] == '.')
        continue;
      if (count < maxCount) {
        strncpy(names + (size_t)count * stride, fd.cFileName, stride - 1);
        names[(size_t)count * stride + stride - 1] = 0;
        count++;
      }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
  }
  qsort(names, count, stride, sFolderNameCompare);
  return count;
}

void datAssetManager::Close(Stream *s) {
  if (s)
    s->Close();
}

Stream *datAssetManager::Open(const char *folder, const char *base,
                              const char *ext) {
  PushFolder(folder);
  Stream *s = Open(base, ext);
  PopFolder();
  return s;
}

void datAssetManager::FullPath(char *dest, int maxLen, const char *base,
                               const char *ext) {
  dest[0] = '\0';
  if (m_RootPath[0] != '\0') {
    strcpy(dest, m_RootPath);
    strcat(dest, "/");
  }
  // "$/" names the asset root: the rest of the name is taken relative to it,
  // ignoring the pushed folder ("$/resources/city/sd_bnd").
  if (base && base[0] == '$' && (base[1] == '/' || base[1] == '\\')) {
    AddExtension(dest, base + 2, ext);
  } else {
    if (m_CurrentFolder[0] != '\0') {
      strcat(dest, m_CurrentFolder);
      strcat(dest, "/");
    }
    AddExtension(dest, base, ext);
  }
  for (int i = 0; dest[i] != '\0'; i++) {
    if (dest[i] == '\\') {
      dest[i] = '/';
    }
  }
}

Stream *datAssetManager::Create(const char *base, const char *ext) {
  char fullPath[512];
  FullPath(fullPath, sizeof(fullPath), base, ext);

  // The low-level create opens a file but will not make the folders above it,
  // so anything writing an asset back into a tree that is not there yet (the
  // rider export) would fail on the very first file.
  coreFileCreatePath(fullPath);

  return Stream::Create(fullPath);
}

bool datAssetManager::Exists(const char *base, const char *ext) {
  char fullPath[512];
  FullPath(fullPath, sizeof(fullPath), base, ext);

  if (zipFile::ExistsInArchives(fullPath))
    return true;

  FILE *f = fopen(fullPath, "rb");
  if (f) {
    fclose(f);
    return true;
  }
  return false;
}

bool datAssetManager::Exists(const char *subfolder, const char *base,
                             const char *ext) {
  char fullPath[512];
  fullPath[0] = '\0';

  if (m_RootPath[0] != '\0') {
    strcpy(fullPath, m_RootPath);
    strcat(fullPath, "/");
  }
  if (m_CurrentFolder[0] != '\0') {
    strcat(fullPath, m_CurrentFolder);
    strcat(fullPath, "/");
  }
  if (subfolder && subfolder[0] != '\0') {
    strcat(fullPath, subfolder);
    strcat(fullPath, "/");
  }
  if (base)
    strcat(fullPath, base);
  if (base && ext && ext[0] != '\0' && !sHasExtension(base, ext)) {
    strcat(fullPath, ".");
    strcat(fullPath, ext);
  }

  for (int i = 0; fullPath[i] != '\0'; i++) {
    if (fullPath[i] == '\\') {
      fullPath[i] = '/';
    }
  }

  if (zipFile::ExistsInArchives(fullPath))
    return true;

  FILE *f = fopen(fullPath, "rb");
  if (f) {
    fclose(f);
    return true;
  }
  return false;
}

Stream *datAssetManager::Open(const char *folder, const char *base,
                              const char *ext, bool ignorePrefix, bool) {
  PushFolder(folder);
  Stream *s = Open(base, ext, ignorePrefix);
  PopFolder();
  return s;
}

Stream *datAssetManager::Create(const char *folder, const char *base,
                                const char *ext) {
  PushFolder(folder);
  Stream *s = Create(base, ext);
  PopFolder();
  return s;
}

void datAssetManager::FullPath(char *dest, int maxLen, const char *folder,
                               const char *base, const char *ext) {
  PushFolder(folder);
  FullPath(dest, maxLen, base, ext);
  PopFolder();
}

void datAssetManager::PushExtension(const char *ext) {
  if (!ext || m_ExtensionDepth >= (int)(sizeof(m_Extensions) / sizeof(m_Extensions[0])))
    return;
  strncpy(m_Extensions[m_ExtensionDepth], ext, sizeof(m_Extensions[0]) - 1);
  m_Extensions[m_ExtensionDepth][sizeof(m_Extensions[0]) - 1] = '\0';
  m_ExtensionDepth++;
}

void datAssetManager::PopExtension() {
  if (m_ExtensionDepth > 0)
    m_ExtensionDepth--;
}

void datAssetManager::BaseName(const char *path, char *dest, int maxLen) const {
  if (!dest || maxLen <= 0)
    return;
  dest[0] = '\0';
  if (!path)
    return;
  const char *start = path;
  for (const char *p = path; *p; p++) {
    if (*p == '/' || *p == '\\' || *p == ':')
      start = p + 1;
  }
  strncpy(dest, start, maxLen - 1);
  dest[maxLen - 1] = '\0';
  char *dot = strrchr(dest, '.');
  if (dot)
    *dot = '\0';
}

int datAssetManager::EnumFiles(const char *folder, EnumCallback cb,
                               void *userData, bool forceRaw) {
  if (!cb)
    return 0;
  char pattern[512];
  if (forceRaw) {
    strncpy(pattern, folder ? folder : ".", sizeof(pattern) - 3);
    pattern[sizeof(pattern) - 3] = '\0';
  } else {
    FullPath(pattern, sizeof(pattern) - 3, folder, "");
  }
  size_t n = strlen(pattern);
  while (n && (pattern[n - 1] == '.' || pattern[n - 1] == '/' || pattern[n - 1] == '\\'))
    pattern[--n] = 0;
  strcat(pattern, "/*");

  WIN32_FIND_DATAA fd;
  HANDLE h = FindFirstFileA(pattern, &fd);
  int count = 0;
  if (h != INVALID_HANDLE_VALUE) {
    do {
      if (fd.cFileName[0] == '.' && (fd.cFileName[1] == '\0' || (fd.cFileName[1] == '.' && fd.cFileName[2] == '\0')))
        continue;
      cb(fd.cFileName, (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0, userData);
      count++;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
  }
  return count;
}