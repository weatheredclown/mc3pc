////////////////////////////////////////
// parser.h
//
// datParser class for reading and writing structured .play / .atdt asset files.
////////////////////////////////////////

#ifndef DATA_PARSER_H
#define DATA_PARSER_H

#include "core/output.h"
#include "core/types.h"
#include "data/callback.h"
#include "data/token.h"
#include "vector/vector3.h"
#include "vector/Vector4.h"
#include <map>
#include "atl/array.h"
#include <string>

class datParser {
public:
    // Class lifetime hooks the game calls around its parsing.  Both are empty
    // in the original too (parse.obj: an 8-byte "jr ra" at 0x5147d8 and
    // 0x5147e0) - the parser keeps no shared state to set up or tear down.
    static void InitClass() { }
    static void ShutdownClass() { }
public:
    datParser(const char *blockName);
    ~datParser();

    void AddValue(const char *name, int *val);
    void AddValue(const char *name, unsigned *val);
    void AddValue(const char *name, s8 *val);
    void AddValue(const char *name, u8 *val);
    void AddValue(const char *name, s16 *val);
    void AddValue(const char *name, u16 *val);
    void AddValue(const char *name, float *val);
    void AddValue(const char *name, double *val);
    void AddValue(const char *name, bool *val);
    void AddValue(const char *name, class Vector2 *val);
    void AddValue(const char *name, Vector3 *val);
    void AddValue(const char *name, Vector4 *val);
    template <typename T>
    void AddValue(const char *name, T *val) { AddValue(name, (int*)val); }
    template <typename T>
    void AddValue(const char *name, T *val, const datCallback &cb) { AddValue(name, val); }
    template <typename T>
    void AddValue(const char *name, T *val, datCallback *cb) { AddValue(name, val); }
    void AddValue(const char *name, float *val, datCallback *cb) { AddValue(name, val); }
    void AddValue(const char *name, int *val, datCallback *cb) { AddValue(name, val); }
    void AddValue(const char *name, class Vector2 *val, datCallback *cb) { AddValue(name, val); }
    void AddValue(const char *name, Vector3 *val, datCallback *cb) { AddValue(name, val); }
    void AddValue(const char *name, Vector4 *val, datCallback *cb) { AddValue(name, val); }
    void AddValueStr(const char *name, char *str, int maxLen);
    // Fixed-size arrays written as a run of values after the key.
    void AddArray(const char *name, int *val, int count);
    void AddArray(const char *name, float *val, int count);
    void AddArray(const char *name, u8 *val, int count);
    void AddArray(const char *name, s16 *val, int count);
    void AddArray(const char *name, class Vector3 *val, int count);   // count vectors = count*3 floats
    datParser* AddParser(const char *name);

    // Keys that name an external file: on load the callback receives the
    // token that followed the key (e.g. "skel foo" -> cb("foo")).
    void RegisterLoader(const char *name, const char *ext, const datCallback &cb);

    bool Load(const char *filename, const char *extension = nullptr);
    bool Save(const char *filename, const char *extension = nullptr);
    // Folder-qualified forms (AGE 2.72): folder is pushed on the asset stack.
    // `full` on Save writes every binding even when it holds its default.
    bool Load(const char *folder, const char *filename, const char *extension);
    bool Save(const char *folder, const char *filename, const char *extension, bool full);
    bool Save(const char *folder, const char *filename, const char *extension) { return Save(folder, filename, extension, false); }

    // Parse from an already-open stream / tokenizer (the caller positions it).
    bool Load(datAsciiTokenizer &tok);
    // Parse one block body from the tokenizer's current position: consumes
    // the opening '{' if present, the bindings, and the closing '}'.  For
    // data embedded inline in another file (e.g. audio curves inside an
    // .audEventData group).
    bool LoadBlock(datAsciiTokenizer &tok);

    void SetNoWarnings(bool b) { m_NoWarnings = b; }
    bool GetNoWarnings() const { return m_NoWarnings; }
    const char *GetBlockName() const { return m_BlockName.c_str(); }

private:
    enum BindingType {
        BIND_INT,
        BIND_UINT,
        BIND_S8,
        BIND_U8,
        BIND_S16,
        BIND_U16,
        BIND_FLOAT,
        BIND_DOUBLE,
        BIND_BOOL,
        BIND_VECTOR2,
        BIND_VECTOR3,
        BIND_VECTOR4,
        BIND_STRING,
        BIND_INT_ARRAY,
        BIND_FLOAT_ARRAY,
        BIND_U8_ARRAY,
        BIND_S16_ARRAY,
        BIND_LOADER,
        BIND_PARSER
    };

    struct Binding {
        BindingType type;
        void *ptr;
        int maxLen;       // string capacity / array count
        datCallback cb;   // BIND_LOADER
        std::string ext;  // BIND_LOADER
    };

    void Add(const char *name, BindingType type, void *ptr, int maxLen = 0);

    bool HasBinding(const char *name) const {
        return m_Bindings.find(name) != m_Bindings.end();
    }

    void ParseBlock(datAsciiTokenizer &tok);
    void ReadValue(const Binding &b, datAsciiTokenizer &tok);
    void SkipValue(datAsciiTokenizer &tok);
    void SkipBlock(datAsciiTokenizer &tok);
    void Write(FILE *f, int indent);

    std::string m_BlockName;
    bool m_NoWarnings;
    std::map<std::string, atArray<Binding>> m_Bindings;
    atArray<std::string> m_OrderedKeys;
    atArray<datParser*> m_SubParsers;
};

#endif // DATA_PARSER_H
