////////////////////////////////////////
// parser.cpp
//
// datParser implementation.
////////////////////////////////////////

#include "data/parser.h"
#include "data/assetcfg.h"
#include "core/output.h"
#include "vector/vector2.h"
#include <stdio.h>
#include <string.h>

datParser::datParser(const char *blockName)
    : m_BlockName(blockName ? blockName : "")
    , m_NoWarnings(false)
{
}

datParser::~datParser()
{
    for (auto sub : m_SubParsers) {
        delete sub;
    }
}

void datParser::Add(const char *name, BindingType type, void *ptr, int maxLen)
{
    Binding b;
    b.type = type;
    b.ptr = ptr;
    b.maxLen = maxLen;
    m_Bindings[name].Append(b);

    std::string stripped;
    for (const char *p = name; *p; p++) {
        if (!isspace((unsigned char)*p)) stripped += *p;
    }
    if (stripped != name) {
        m_Bindings[stripped].Append(b);
    }

    m_OrderedKeys.Append(name);
}

void datParser::AddValue(const char *name, int *val)        { Add(name, BIND_INT, val); }
void datParser::AddValue(const char *name, unsigned *val)   { Add(name, BIND_UINT, val); }
void datParser::AddValue(const char *name, s8 *val)         { Add(name, BIND_S8, val); }
void datParser::AddValue(const char *name, u8 *val)         { Add(name, BIND_U8, val); }
void datParser::AddValue(const char *name, s16 *val)        { Add(name, BIND_S16, val); }
void datParser::AddValue(const char *name, u16 *val)        { Add(name, BIND_U16, val); }
void datParser::AddValue(const char *name, float *val)      { Add(name, BIND_FLOAT, val); }
void datParser::AddValue(const char *name, double *val)     { Add(name, BIND_DOUBLE, val); }
void datParser::AddValue(const char *name, bool *val)       { Add(name, BIND_BOOL, val); }
void datParser::AddValue(const char *name, Vector2 *val)    { Add(name, BIND_VECTOR2, val); }
void datParser::AddValue(const char *name, Vector3 *val)    { Add(name, BIND_VECTOR3, val); }
void datParser::AddValue(const char *name, Vector4 *val)    { Add(name, BIND_VECTOR4, val); }
void datParser::AddValueStr(const char *name, char *str, int maxLen) { Add(name, BIND_STRING, str, maxLen); }
void datParser::AddArray(const char *name, int *val, int count)     { Add(name, BIND_INT_ARRAY, val, count); }
void datParser::AddArray(const char *name, float *val, int count)   { Add(name, BIND_FLOAT_ARRAY, val, count); }
void datParser::AddArray(const char *name, u8 *val, int count)      { Add(name, BIND_U8_ARRAY, val, count); }
void datParser::AddArray(const char *name, s16 *val, int count)     { Add(name, BIND_S16_ARRAY, val, count); }

void datParser::RegisterLoader(const char *name, const char *ext, const datCallback &cb)
{
    Binding b;
    b.type = BIND_LOADER;
    b.ptr = 0;
    b.maxLen = 0;
    b.cb = cb;
    b.ext = ext ? ext : "";
    m_Bindings[name].Append(b);
    m_OrderedKeys.Append(name);
}

datParser* datParser::AddParser(const char *name)
{
    datParser *sub = new datParser(name);
    Add(name, BIND_PARSER, sub);
    m_SubParsers.Append(sub);
    return sub;
}

bool datParser::Load(const char *filename, const char *extension)
{
    Stream *s = ASSET.Open(filename, extension);
    if (!s) {
        if (!m_NoWarnings) {
            Warningf("datParser::Load - failed to open '%s.%s'", filename, extension ? extension : "");
        }
        return false;
    }

    datTokenizer tok(filename, s);
    tok.CommentChar = '\0';
    bool found = Load(tok);
    s->Close();
    return found;
}

bool datParser::Load(const char *folder, const char *filename, const char *extension)
{
    if (folder && folder[0]) ASSET.PushFolder(folder);
    bool ok = Load(filename, extension);
    if (folder && folder[0]) ASSET.PopFolder();
    return ok;
}

bool datParser::Save(const char *folder, const char *filename, const char *extension, bool)
{
    char path[512];
    ASSET.FullPath(path, sizeof(path), folder, filename, extension);
    return Save(path, 0);
}

bool datParser::Load(datAsciiTokenizer &tok)
{
    int start = 0;
    // Skip initial header fields (e.g. type: a)
    if (tok.CheckToken("type", true)) {
        tok.CheckToken(":", true); // consume optional colon
        char dummy[256];
        tok.GetToken(dummy, sizeof(dummy));
    }

    // Find the target block
    char token[256];
    bool sawAny = false;
    while (true) {
        tok.GetToken(token, sizeof(token));
        if (token[0] == '\0') break;
        sawAny = true;

        if (strcmp(token, m_BlockName.c_str()) == 0) {
            tok.MatchToken("{");
            ParseBlock(tok);
            return true;
        }
    }

    // No named block: tune files written by parFileIO carry the key/value
    // pairs at top level.  Re-scan from the start treating the file as the
    // block body.
    if (sawAny && tok.GetStream()) {
        Stream *s = tok.GetStream();
        s->Seek(start);
        tok.Init(tok.GetName(), s);
        tok.CommentChar = '\0';
        if (tok.CheckToken("type", true)) {
            tok.CheckToken(":", true);
            char dummy[256];
            tok.GetToken(dummy, sizeof(dummy));
        }
        ParseBlock(tok);
        return true;
    }
    return false;
}

bool datParser::LoadBlock(datAsciiTokenizer &tok)
{
    tok.CheckToken("{", true);
    ParseBlock(tok);
    return true;
}

bool datParser::Save(const char *filename, const char *extension)
{
    // Write out back to disk (usually entity.tune or local file)
    char fullpath[512];
    if (extension && extension[0] != '\0') {
        sprintf(fullpath, "%s.%s", filename, extension);
    } else {
        strcpy(fullpath, filename);
    }

    FILE *f = fopen(fullpath, "w");
    if (!f) {
        Warningf("datParser::Save - failed to create '%s'", fullpath);
        return false;
    }

    fprintf(f, "type: a\n");
    Write(f, 0);
    fclose(f);
    return true;
}

void datParser::ReadValue(const Binding &b, datAsciiTokenizer &tok)
{
    switch (b.type) {
    case BIND_PARSER:
        tok.MatchToken("{");
        ((datParser*)b.ptr)->ParseBlock(tok);
        break;
    case BIND_INT:      *(int*)b.ptr = tok.GetInt(); break;
    case BIND_UINT:     *(unsigned*)b.ptr = (unsigned)tok.GetInt(); break;
    case BIND_S8:       *(s8*)b.ptr = (s8)tok.GetInt(); break;
    case BIND_U8:       *(u8*)b.ptr = (u8)tok.GetInt(); break;
    case BIND_S16:      *(s16*)b.ptr = (s16)tok.GetInt(); break;
    case BIND_U16:      *(u16*)b.ptr = (u16)tok.GetInt(); break;
    case BIND_FLOAT:    *(float*)b.ptr = tok.GetFloat(); break;
    case BIND_DOUBLE:   *(double*)b.ptr = (double)tok.GetFloat(); break;
    case BIND_BOOL: {
        // accept 0/1 and true/false
        if (tok.CheckToken("true", true)) *(bool*)b.ptr = true;
        else if (tok.CheckToken("false", true)) *(bool*)b.ptr = false;
        else *(bool*)b.ptr = (tok.GetInt() != 0);
        break;
    }
    case BIND_VECTOR2: {
        Vector2 *v = (Vector2*)b.ptr;
        v->x = tok.GetFloat();
        v->y = tok.GetFloat();
        break;
    }
    case BIND_VECTOR3:  tok.GetVector(*(Vector3*)b.ptr); break;
    case BIND_VECTOR4:  tok.GetVector(*(Vector4*)b.ptr); break;
    case BIND_STRING:   tok.GetToken((char*)b.ptr, b.maxLen); break;
    case BIND_INT_ARRAY:
        for (int i = 0; i < b.maxLen; i++) ((int*)b.ptr)[i] = tok.GetInt();
        break;
    case BIND_FLOAT_ARRAY:
        for (int i = 0; i < b.maxLen; i++) ((float*)b.ptr)[i] = tok.GetFloat();
        break;
    case BIND_U8_ARRAY:
        for (int i = 0; i < b.maxLen; i++) ((u8*)b.ptr)[i] = (u8)tok.GetInt();
        break;
    case BIND_S16_ARRAY:
        for (int i = 0; i < b.maxLen; i++) ((s16*)b.ptr)[i] = (s16)tok.GetInt();
        break;
    case BIND_LOADER: {
        char name[256];
        tok.GetToken(name, sizeof(name));
        b.cb.Call((void*)name);
        break;
    }
    }
}

void datParser::ParseBlock(datAsciiTokenizer &tok)
{
    std::map<std::string, int> matchCounts;
    char token[256];

    while (true) {
        tok.GetToken(token, sizeof(token));
        if (token[0] == '\0' || strcmp(token, "}") == 0) {
            break;
        }

        if (strcmp(token, "{") == 0) {
            continue;
        }

        if (HasBinding(token)) {
            int idx = matchCounts[token]++;
            const auto &list = m_Bindings[token];
            if (idx < list.GetCount()) {
                ReadValue(list[idx], tok);
            } else {
                SkipValue(tok);
            }
        } else {
            SkipValue(tok);
        }
    }
}

void datParser::SkipValue(datAsciiTokenizer &tok)
{
    if (tok.CheckToken("{", true)) {
        SkipBlock(tok);
    } else {
        while (true) {
            const char* peek = tok.GetCurrentToken();
            if (peek[0] == '\0' || strcmp(peek, "}") == 0 || strcmp(peek, "{") == 0 || HasBinding(peek)) {
                break;
            }
            char dummy[256];
            tok.GetToken(dummy, sizeof(dummy));
        }
    }
}

void datParser::SkipBlock(datAsciiTokenizer &tok)
{
    int depth = 1;
    char token[256];
    while (depth > 0) {
        tok.GetToken(token, sizeof(token));
        if (token[0] == '\0') break;
        if (strcmp(token, "{") == 0) depth++;
        else if (strcmp(token, "}") == 0) depth--;
    }
}

void datParser::Write(FILE *f, int indent)
{
    std::string ind(indent, ' ');
    fprintf(f, "%s%s {\n", ind.c_str(), m_BlockName.c_str());

    std::map<std::string, int> writeCounts;
    for (const auto &key : m_OrderedKeys) {
        int idx = writeCounts[key]++;
        const auto &list = m_Bindings[key];
        if (idx >= list.GetCount()) continue;
        const auto &b = list[idx];
        if (b.type == BIND_PARSER) {
            ((datParser*)b.ptr)->Write(f, indent + 2);
            continue;
        }
        if (b.type == BIND_LOADER) continue;   // written by the owner

        fprintf(f, "%s  %s ", ind.c_str(), key.c_str());
        switch (b.type) {
        case BIND_INT:      fprintf(f, "%d \n", *(int*)b.ptr); break;
        case BIND_UINT:     fprintf(f, "%u \n", *(unsigned*)b.ptr); break;
        case BIND_S8:       fprintf(f, "%d \n", (int)*(s8*)b.ptr); break;
        case BIND_U8:       fprintf(f, "%d \n", (int)*(u8*)b.ptr); break;
        case BIND_S16:      fprintf(f, "%d \n", (int)*(s16*)b.ptr); break;
        case BIND_U16:      fprintf(f, "%d \n", (int)*(u16*)b.ptr); break;
        case BIND_FLOAT:    fprintf(f, "%f \n", *(float*)b.ptr); break;
        case BIND_DOUBLE:   fprintf(f, "%f \n", *(double*)b.ptr); break;
        case BIND_BOOL:     fprintf(f, "%d \n", *(bool*)b.ptr ? 1 : 0); break;
        case BIND_VECTOR2: {
            Vector2 *v = (Vector2*)b.ptr;
            fprintf(f, "%f\t%f \n", v->x, v->y);
            break;
        }
        case BIND_VECTOR3: {
            Vector3 *v = (Vector3*)b.ptr;
            fprintf(f, "%f\t%f\t%f \n", v->x, v->y, v->z);
            break;
        }
        case BIND_VECTOR4: {
            Vector4 *v = (Vector4*)b.ptr;
            fprintf(f, "%f\t%f\t%f\t%f \n", v->x, v->y, v->z, v->w);
            break;
        }
        case BIND_STRING:   fprintf(f, "\"%s\" \n", (char*)b.ptr); break;
        case BIND_INT_ARRAY:
            for (int i = 0; i < b.maxLen; i++) fprintf(f, "%d ", ((int*)b.ptr)[i]);
            fprintf(f, "\n");
            break;
        case BIND_FLOAT_ARRAY:
            for (int i = 0; i < b.maxLen; i++) fprintf(f, "%f ", ((float*)b.ptr)[i]);
            fprintf(f, "\n");
            break;
        case BIND_U8_ARRAY:
            for (int i = 0; i < b.maxLen; i++) fprintf(f, "%d ", (int)((u8*)b.ptr)[i]);
            fprintf(f, "\n");
            break;
        case BIND_S16_ARRAY:
            for (int i = 0; i < b.maxLen; i++) fprintf(f, "%d ", (int)((s16*)b.ptr)[i]);
            fprintf(f, "\n");
            break;
        default: fprintf(f, "\n"); break;
        }
    }

    fprintf(f, "%s}\n", ind.c_str());
}

void datParser::AddArray(const char *name, Vector3 *val, int count)  { Add(name, BIND_FLOAT_ARRAY, val, count * 3); }
