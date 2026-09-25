#include <stdio.h>
////////////////////////////////////////
// token.cpp
////////////////////////////////////////

#include "data/token.h"
#include "core/output.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

datAsciiTokenizer::datAsciiTokenizer() {
  m_Stream = NULL;
  m_Buffer = NULL;
  m_BufLen = 0;
  m_Cursor = 0;
  m_CurrentToken[0] = '\0';
  m_HasToken = false;
  m_Primed = false;
  CommentChar = ';';
  filename[0] = '\0';
  nextch = -1;
  S = NULL;
  line = 1;
  m_Terminators[0] = '\0';
  m_Indent = 0;
}

datAsciiTokenizer::~datAsciiTokenizer() {
  if (m_Buffer) {
    delete[] m_Buffer;
  }
}

void datAsciiTokenizer::Init(const char *name, Stream *stream) {
  m_Stream = stream;
  S = stream;
  line = 1;
  m_Indent = 0;
  if (S) {
    nextch = S->FastGetCh();
  } else {
    nextch = -1;
  }
  if (name) {
    strncpy(filename, name, sizeof(filename) - 1);
    filename[sizeof(filename) - 1] = '\0';
  } else {
    filename[0] = '\0';
  }

  if (m_Buffer) {
    delete[] m_Buffer;
    m_Buffer = NULL;
  }
  m_BufLen = 0;
  m_Cursor = 0;
  m_CurrentToken[0] = '\0';
  m_HasToken = false;
  m_Primed = false;

  if (!stream)
    return;

  // Rewind: the FastGetCh above consumed byte 0, so fill the buffer from the
  // start of the stream (otherwise the very first token loses its first char).
  stream->Seek(0);

  int cap = 4096;
  m_Buffer = new char[cap];
  while (true) {
    int readBytes = stream->Read(m_Buffer + m_BufLen, cap - m_BufLen - 1);
    if (readBytes <= 0)
      break;
    m_BufLen += readBytes;
    if (m_BufLen >= cap - 1) {
      cap *= 2;
      char *newBuf = new char[cap];
      memcpy(newBuf, m_Buffer, m_BufLen);
      delete[] m_Buffer;
      m_Buffer = newBuf;
    }
  }
  m_Buffer[m_BufLen] = '\0';

  if (S) {
    S->Seek(0);
    nextch = S->FastGetCh();
  }

  // Do not read the first token here: the caller may still set CommentChar
  // before its first read (the FSM loader sets ';' only after Init()). The
  // token is materialized lazily on first access via EnsurePrimed().
}

void datAsciiTokenizer::Init(void) { Init(NULL, NULL); }

void datAsciiTokenizer::SetTerminators(const char* terminators) {
  if (terminators) {
    strncpy(m_Terminators, terminators, sizeof(m_Terminators) - 1);
    m_Terminators[sizeof(m_Terminators) - 1] = '\0';
  } else {
    m_Terminators[0] = '\0';
  }
}

bool datAsciiTokenizer::IsTerminator(char c) const {
  if (c == '\0') return false;
  return strchr(m_Terminators, c) != NULL;
}

bool datAsciiTokenizer::SkipCommentChars() {
  if (!m_Buffer || m_Cursor >= m_BufLen)
    return false;

  char c = m_Buffer[m_Cursor];

  if (CommentChar != '\0' && c == CommentChar) {
    m_Cursor++;
    while (m_Cursor < m_BufLen && m_Buffer[m_Cursor] != '\n' &&
           m_Buffer[m_Cursor] != '\r') {
      m_Cursor++;
    }
    return true;
  }

  return false;
}

void datAsciiTokenizer::NextToken() {
  m_CurrentToken[0] = '\0';
  m_HasToken = false;

  if (!m_Buffer || m_Cursor >= m_BufLen)
    return;

  while (m_Cursor < m_BufLen) {
    char c = m_Buffer[m_Cursor];
    if (isspace((unsigned char)c) || IsTerminator(c)) {
      if (c == '\n') {
        line++;
      }
      m_Cursor++;
      continue;
    }

    if (SkipCommentChars()) {
      continue;
    }

    break;
  }

  if (m_Cursor >= m_BufLen)
    return;

  char c = m_Buffer[m_Cursor];
  int tokenLen = 0;
  bool isQuoted = false;

  if (c == '"') {
    isQuoted = true;
    m_Cursor++;
    while (m_Cursor < m_BufLen && m_Buffer[m_Cursor] != '"') {
      if (tokenLen < sizeof(m_CurrentToken) - 1) {
        m_CurrentToken[tokenLen++] = m_Buffer[m_Cursor];
      }
      m_Cursor++;
    }
    if (m_Cursor < m_BufLen) {
      m_Cursor++;
    }
  } else if (c == '<') {
    m_CurrentToken[tokenLen++] = c;
    m_Cursor++;
    if (m_Cursor < m_BufLen && (m_Buffer[m_Cursor] == '/' || m_Buffer[m_Cursor] == '!' || m_Buffer[m_Cursor] == '?')) {
      m_CurrentToken[tokenLen++] = m_Buffer[m_Cursor++];
    }
  } else if (c == '?' || (c == '/' && m_Cursor + 1 < m_BufLen && m_Buffer[m_Cursor + 1] == '>')) {
    m_CurrentToken[tokenLen++] = c;
    m_Cursor++;
    if (m_Cursor < m_BufLen && m_Buffer[m_Cursor] == '>') {
      m_CurrentToken[tokenLen++] = m_Buffer[m_Cursor++];
    }
  } else if (c == '{' || c == '}' || c == '=' || c == '>') {
    m_CurrentToken[tokenLen++] = c;
    m_Cursor++;
  } else {
    while (m_Cursor < m_BufLen) {
      char nextC = m_Buffer[m_Cursor];
      if (isspace((unsigned char)nextC) || IsTerminator(nextC) || nextC == '{' || nextC == '}' ||
          nextC == '=' || (CommentChar != '\0' && nextC == CommentChar) || nextC == '"' || nextC == '<' || nextC == '>' ||
          nextC == '?') {   // '/' stays inside tokens: AGE tune paths ("Reward/Rmagnum")
        break;
      }
      if (nextC == '/' && m_Cursor + 1 < m_BufLen &&
          m_Buffer[m_Cursor + 1] == '/') {
        break;
      }
      if (tokenLen < sizeof(m_CurrentToken) - 1) {
        m_CurrentToken[tokenLen++] = nextC;
      }
      m_Cursor++;
    }
  }

  m_CurrentToken[tokenLen] = '\0';
  m_HasToken = (tokenLen > 0 || isQuoted);
  if (S) {
    S->Seek(m_Cursor);
    if (m_Buffer && m_Cursor >= 0 && m_Cursor < m_BufLen) {
      nextch = (unsigned char)m_Buffer[m_Cursor];
    } else {
      nextch = -1;
    }
  }
}

void datAsciiTokenizer::SyncWithStream() {
  if (S && m_Buffer) {
    int sTell = S->Tell();
    if (sTell != m_Cursor + 1 && sTell != m_Cursor) {
      m_Cursor = sTell;
      if (m_Cursor < 0) m_Cursor = 0;
      if (m_Cursor > m_BufLen) m_Cursor = m_BufLen;
      if (m_Cursor < m_BufLen) {
        nextch = (unsigned char)m_Buffer[m_Cursor];
      } else {
        nextch = -1;
      }
      m_Primed = false;
      m_HasToken = false;
    }
  }
}

void datAsciiTokenizer::EnsurePrimed() {
  SyncWithStream();
  if (!m_Primed) {
    m_Primed = true;
    NextToken();
  }
}

void datAsciiTokenizer::SkipPendingComments() {
  EnsurePrimed();
  while (m_HasToken && CommentChar != '\0' && m_CurrentToken[0] == CommentChar) {
    NextToken();
  }
}

bool datAsciiTokenizer::CheckToken(const char *token, bool consume) {
  SkipPendingComments();
  if (!m_HasToken)
    return false;

  if (_stricmp(m_CurrentToken, token) == 0) {
    if (consume) {
      NextToken();
    }
    return true;
  }

  int tokenLen = (int)strlen(token);
  int currLen = (int)strlen(m_CurrentToken);
  if (currLen > 1 && m_CurrentToken[currLen - 1] == ':') {
    if (currLen - 1 == tokenLen &&
        _strnicmp(m_CurrentToken, token, tokenLen) == 0) {
      if (consume) {
        NextToken();
      }
      return true;
    }
  }

  return false;
}

void datAsciiTokenizer::MatchToken(const char *token) {
  if (!CheckToken(token, true)) {
    Quitf("datAsciiTokenizer: [%s:%d] Expected token '%s' but found '%s'", filename, line, token,
          m_CurrentToken);
  }
}

// AGE loaders read the keys they know and Pop() the rest: the tokenizer
// discards everything up to the block's closing brace, tracking nested
// blocks, instead of insisting the next token be '}'.
// Skipped data is reported once per file+key so newer-than-the-code
// content (e.g. the Remix disc's NumLaps/CopCarTypes race keys) is visible.
static void PopWarnSkipped(const char *file, const char *firstKey, int skipped) {
  enum { kMax = 128 };
  static char s_Seen[kMax][96];
  static int s_Count = 0;
  char key[96];
  snprintf(key, sizeof(key), "%s:%s", file ? file : "?", firstKey);
  for (int i = 0; i < s_Count; i++)
    if (strcmp(s_Seen[i], key) == 0)
      return;
  if (s_Count < kMax) {
    strncpy(s_Seen[s_Count], key, sizeof(s_Seen[0]) - 1);
    s_Seen[s_Count][sizeof(s_Seen[0]) - 1] = 0;
    s_Count++;
  }
  Warningf("datAsciiTokenizer: %s: Pop() skipped %d unread token(s) starting at '%s'", file ? file : "?", skipped, firstKey);
}

void datAsciiTokenizer::Pop() {
  SkipPendingComments();
  int depth = 0;
  int skipped = 0;
  char firstKey[64] = "";
  while (m_HasToken) {
    if (strcmp(m_CurrentToken, "}") == 0) {
      NextToken();
      if (depth == 0) {
        if (skipped)
          PopWarnSkipped(GetName(), firstKey, skipped);
        return;
      }
      depth--;
    } else {
      if (strcmp(m_CurrentToken, "{") == 0)
        depth++;
      if (!skipped) {
        strncpy(firstKey, m_CurrentToken, sizeof(firstKey) - 1);
        firstKey[sizeof(firstKey) - 1] = 0;
      }
      NextToken();
    }
    skipped++;
    SkipPendingComments();
  }
  Quitf("datAsciiTokenizer: %s: Expected token '}' but reached EOF (block opened before '%s' was never closed)", GetName(), firstKey);
}

void datAsciiTokenizer::GetDelimiter(const char *delim) {
  EnsurePrimed();
  if (!m_HasToken) {
    Quitf("datAsciiTokenizer: Expected delimiter '%s' but reached EOF", delim);
  }
  if (strcmp(m_CurrentToken, delim) != 0) {
    Quitf("datAsciiTokenizer: Expected delimiter '%s' but found '%s'", delim,
          m_CurrentToken);
  }
  NextToken();
}

int datAsciiTokenizer::GetToken(char *buffer, int maxLen) {
  SkipPendingComments();
  if (!m_HasToken) {
    if (buffer && maxLen > 0)
      buffer[0] = '\0';
    return 0;
  }
  int len = (int)strlen(m_CurrentToken);
  if (buffer && maxLen > 0) {
    strncpy(buffer, m_CurrentToken, maxLen - 1);
    buffer[maxLen - 1] = '\0';
  }
  NextToken();
  return len;
}

float datAsciiTokenizer::GetFloat() {
  SkipPendingComments();
  if (!m_HasToken)
    return 0.0f;
  float val = (float)atof(m_CurrentToken);
  NextToken();
  return val;
}

int datAsciiTokenizer::GetInt() {
  SkipPendingComments();
  if (!m_HasToken)
    return 0;
  int val = atoi(m_CurrentToken);
  NextToken();
  return val;
}

void datAsciiTokenizer::GetVector(Vector2 &v) {
  v.x = GetFloat();
  v.y = GetFloat();
}

void datAsciiTokenizer::GetVector(Vector3 &v) {
  v.x = GetFloat();
  v.y = GetFloat();
  v.z = GetFloat();
}

void datAsciiTokenizer::GetVector(Vector4 &v) {
  v.x = GetFloat();
  v.y = GetFloat();
  v.z = GetFloat();
  v.w = GetFloat();
}

#include <stdarg.h>

void datAsciiTokenizer::PutStr(const char *fmt, ...) {
  if (!m_Stream)
    return;
  va_list args;
  va_start(args, fmt);
  char buf[1024];
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  m_Stream->Write(buf, (int)strlen(buf));
}

void datAsciiTokenizer::Put(const char *str) {
  if (!m_Stream || !str)
    return;
  m_Stream->Write(str, (int)strlen(str));
}

void datAsciiTokenizer::PutDelimiter(const char *str) { Put(str); }

void datAsciiTokenizer::WriteTabs(int count) {
  for (int i = 0; i < count; i++)
    Put("\t");
}

void datAsciiTokenizer::StartBlock() {
  Put("{\n");
  m_Indent++;
}

void datAsciiTokenizer::EndBlock() {
  if (m_Indent > 0)
    m_Indent--;
  StartLine();
  Put("}\n");
}

void datAsciiTokenizer::StartLine() {
  WriteTabs(m_Indent);
}

void datAsciiTokenizer::EndLine() {
  Put("\n");
}

void datAsciiTokenizer::SkipComment() {
  while (nextch != -1 && nextch != '\n' && nextch != '\r') {
    nextch = S->FastGetCh();
  }
  while (nextch == '\n' || nextch == '\r') {
    if (nextch == '\n')
      line++;
    nextch = S->FastGetCh();
  }
}

void datAsciiTokenizer::SkipToEndOfLine() {
  if (!m_Buffer)
    return;
  while (m_Cursor < m_BufLen) {
    char c = m_Buffer[m_Cursor];
    m_Cursor++;
    if (c == '\n') {
      line++;
      break;
    }
    if (c == '\r') {
      if (m_Cursor < m_BufLen && m_Buffer[m_Cursor] == '\n') {
        m_Cursor++;
      }
      line++;
      break;
    }
  }
  NextToken();
}

// Returns the line length; blank lines are skipped so 0 means end of stream
// (callers loop on `GetLine(...) > 0`).
int datAsciiTokenizer::GetLine(char *buffer, int maxLen) {
  SkipPendingComments();
  if (!buffer || maxLen <= 0)
    return 0;
  int len = 0;
  if (m_HasToken) {
    strncpy(buffer, m_CurrentToken, maxLen - 1);
    buffer[maxLen - 1] = '\0';
    len = (int)strlen(buffer);
    m_HasToken = false;
  }
  if (m_Buffer) {
  again:
    while (m_Cursor < m_BufLen) {
      char c = m_Buffer[m_Cursor];
      if (c == '\n' || c == '\r') {
        break;
      }
      if (len < maxLen - 1) {
        buffer[len++] = c;
      }
      m_Cursor++;
    }
    buffer[len] = '\0';
    // Consume the newline characters
    while (m_Cursor < m_BufLen) {
      char c = m_Buffer[m_Cursor];
      if (c == '\n') {
        line++;
        m_Cursor++;
      } else if (c == '\r') {
        m_Cursor++;
      } else {
        break;
      }
    }
    if (len == 0 && m_Cursor < m_BufLen)
      goto again;
  } else {
    buffer[len] = '\0';
  }
  if (S) {
    S->Seek(m_Cursor);
    if (m_Buffer && m_Cursor >= 0 && m_Cursor < m_BufLen) {
      nextch = (unsigned char)m_Buffer[m_Cursor];
    } else {
      nextch = -1;
    }
  }
  m_HasToken = false;
  m_CurrentToken[0] = '\0';
  m_Primed = false;
  return len;
}

void datAsciiTokenizer::PrintFileContext() {
  if (!m_Buffer) {
    printf("[Tokenizer] No buffer to print.\n");
    return;
  }
  printf("--- Tokenizer File Content: %s ---\n", filename);
  int currentLine = 1;
  const char *ptr = m_Buffer;
  const char *lineStart = ptr;
  while (*ptr != '\0') {
    if (*ptr == '\n' || *ptr == '\r') {
      int lineLen = (int)(ptr - lineStart);
      if (currentLine == line) {
        printf("--> %4d: %.*s\n", currentLine, lineLen, lineStart);
      } else {
        printf("    %4d: %.*s\n", currentLine, lineLen, lineStart);
      }
      if (*ptr == '\r' && *(ptr + 1) == '\n') {
        ptr++;
      }
      currentLine++;
      lineStart = ptr + 1;
    }
    ptr++;
  }
  if (ptr > lineStart) {
    int lineLen = (int)(ptr - lineStart);
    if (currentLine == line) {
      printf("--> %4d: %.*s\n", currentLine, lineLen, lineStart);
    } else {
      printf("    %4d: %.*s\n", currentLine, lineLen, lineStart);
    }
  }
  printf("-----------------------------------\n");
  fflush(stdout);
}

//////////////////////////////////////////////////////////////////////////
// datBinTokenizer
//////////////////////////////////////////////////////////////////////////

datBinTokenizer::datBinTokenizer() : m_Stream(NULL), m_Eof(false) {}

datBinTokenizer::datBinTokenizer(const char *name, Stream *stream)
    : m_Stream(NULL), m_Eof(false) {
  Init(name, stream);
}

datBinTokenizer::~datBinTokenizer() {}

void datBinTokenizer::Init(const char *name, Stream *stream) {
  m_Stream = stream;
  m_Eof = false;
  line = 1;
  if (name) {
    strncpy(filename, name, sizeof(filename) - 1);
    filename[sizeof(filename) - 1] = '\0';
  } else {
    filename[0] = '\0';
  }
}

bool datBinTokenizer::AtEnd() const { return m_Eof || !m_Stream; }

bool datBinTokenizer::ReadRaw(void *dst, int bytes) {
  if (!m_Stream || m_Eof) {
    memset(dst, 0, bytes);
    return false;
  }
  int got = m_Stream->Read(dst, bytes);
  if (got != bytes) {
    if (got < 0) got = 0;
    memset((char *)dst + got, 0, bytes - got);
    m_Eof = true;
    return false;
  }
  return true;
}

void datBinTokenizer::WriteRaw(const void *src, int bytes) {
  if (m_Stream) m_Stream->Write(src, bytes);
}

int datBinTokenizer::ReadString(char *buffer, int maxLen) {
  unsigned short len = 0;
  if (!ReadRaw(&len, sizeof(len))) {
    if (buffer && maxLen > 0) buffer[0] = '\0';
    return 0;
  }
  int copied = 0;
  int remaining = len;
  char chunk[64];
  while (remaining > 0) {
    int want = remaining < (int)sizeof(chunk) ? remaining : (int)sizeof(chunk);
    if (!ReadRaw(chunk, want)) break;
    for (int i = 0; i < want; i++) {
      if (buffer && copied < maxLen - 1) buffer[copied++] = chunk[i];
    }
    remaining -= want;
  }
  if (buffer && maxLen > 0) buffer[copied] = '\0';
  return len;
}

int datBinTokenizer::GetToken(char *buffer, int maxLen) {
  return ReadString(buffer, maxLen);
}

int datBinTokenizer::GetLine(char *buffer, int maxLen) {
  int n = ReadString(buffer, maxLen);
  line++;
  return n;
}

bool datBinTokenizer::CheckToken(const char *token, bool consume) {
  if (!m_Stream || m_Eof) return false;
  int start = m_Stream->Tell();
  char buf[256];
  int n = ReadString(buf, sizeof(buf));
  bool match = (n > 0) && !strcmp(buf, token);
  if (!match || !consume) {
    m_Eof = false;
    m_Stream->Seek(start);
  }
  return match;
}

bool datBinTokenizer::CheckIToken(const char *token, bool consume) {
  if (!m_Stream || m_Eof) return false;
  int start = m_Stream->Tell();
  char buf[256];
  int n = ReadString(buf, sizeof(buf));
  bool match = (n > 0) && !_stricmp(buf, token);
  if (!match || !consume) {
    m_Eof = false;
    m_Stream->Seek(start);
  }
  return match;
}

void datBinTokenizer::MatchToken(const char *token) {
  char buf[256];
  ReadString(buf, sizeof(buf));
  if (strcmp(buf, token)) {
    Warningf("%s: binary token mismatch, expected '%s' got '%s'", filename,
             token, buf);
  }
}

void datBinTokenizer::MatchIToken(const char *token) {
  char buf[256];
  ReadString(buf, sizeof(buf));
  if (_stricmp(buf, token)) {
    Warningf("%s: binary token mismatch, expected '%s' got '%s'", filename,
             token, buf);
  }
}

float datBinTokenizer::GetFloat() {
  float f = 0.0f;
  ReadRaw(&f, sizeof(f));
  return f;
}

int datBinTokenizer::GetInt() {
  int i = 0;
  ReadRaw(&i, sizeof(i));
  return i;
}

void datBinTokenizer::GetVector(Vector2 &out) {
  out.x = GetFloat();
  out.y = GetFloat();
}

void datBinTokenizer::GetVector(Vector3 &out) {
  out.x = GetFloat();
  out.y = GetFloat();
  out.z = GetFloat();
}

void datBinTokenizer::GetVector(Vector4 &out) {
  out.x = GetFloat();
  out.y = GetFloat();
  out.z = GetFloat();
  out.w = GetFloat();
}

void datBinTokenizer::PutStr(const char *fmt, ...) {
  char buf[1024];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  buf[sizeof(buf) - 1] = '\0';
  Put(buf);
}

void datBinTokenizer::Put(const char *str) {
  if (!str) str = "";
  size_t n = strlen(str);
  if (n > 0xFFFF) n = 0xFFFF;
  unsigned short len = (unsigned short)n;
  WriteRaw(&len, sizeof(len));
  if (len) WriteRaw(str, len);
}

void datBinTokenizer::Put(const Vector2 &v) {
  Put(v.x);
  Put(v.y);
}

void datBinTokenizer::Put(const Vector3 &v) {
  Put(v.x);
  Put(v.y);
  Put(v.z);
}

void datBinTokenizer::Put(const Vector4 &v) {
  Put(v.x);
  Put(v.y);
  Put(v.z);
  Put(v.w);
}

void datBinTokenizer::Put(int val) { WriteRaw(&val, sizeof(val)); }

void datBinTokenizer::Put(float val) { WriteRaw(&val, sizeof(val)); }
