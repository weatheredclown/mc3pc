#include "rmcore/shader.h"
#include "rmcore/drawable.h"
#include "bank/bank.h"

#include <cstring>

// Initialize static global shader variables array.
rmcShaderData rmcShader::sm_Globals[64];
u32 rmcShader::sm_PassEnable = 0xFFFFFFFF;
rmcShaderFactory *rmcShaderFactory::sm_Instance = nullptr;
rmcShaderGroup *rmcShaderGroup::sm_ParentShaderGroup = nullptr;
int rmcShaderGroup::sm_ParentShaderCount = 0;
rmcShaderGroup *rmcShaderGroup::s_ParentShaderGroup = nullptr;
int rmcShaderGroup::s_ParentShaderCount = 0;

// Sets a global floating-point constant value by index.
void rmcShader::SetGlobal(int index, float val) {
    if (index >= 0 && index < 64) {
        sm_Globals[index].Float = val;
    }
}

// Looks up the index of a global shader variable by name.
int rmcShader::LookupGlobal(const char* name) {
    if (!name) return -1;
    if (_stricmp(name, "time") == 0 || _stricmp(name, "g_Time") == 0 || _stricmp(name, "Time") == 0) {
        return 0;
    }
    return -1;
}

static bool IsTextureName(const char* str) {
    if (!str || str[0] == '\0') return false;
    if (_stricmp(str, "true") == 0 || _stricmp(str, "false") == 0) return false;
    
    // Any alphanumeric string containing letters is considered a texture name
    bool hasLetter = false;
    for (int i = 0; str[i] != '\0'; ++i) {
        if ((str[i] >= 'a' && str[i] <= 'z') || (str[i] >= 'A' && str[i] <= 'Z')) {
            hasLetter = true;
            break;
        }
    }
    return hasLetter;
}

rmcShader::Pass::Stage::Stage()
    : m_Texture(nullptr), m_Texture2(nullptr), m_EnvMap(nullptr), m_TexMtx(nullptr)
    , m_Flags(0), m_TexCoordIndex(0), m_Filter(0), m_TextureCount(0)
{
}

rmcShader::Pass::Stage::Stage(datResource &rsc)
{
#if defined(__WIN32PC)
    u32 start = rsc.Tell();
    rsc.PointerFixup(m_Texture);
    rsc.PointerFixup(m_Texture2);
    rsc.PointerFixup(m_EnvMap);
    ObjectFixup(rsc, m_TexMtx);
    m_Flags = rsc.GetU8();
    m_TexCoordIndex = rsc.GetU8();
    m_Filter = rsc.GetU8();
    m_TextureCount = rsc.GetU8();
    u32 consumed = rsc.Tell() - start;
    (void)consumed;
#else
    rsc.PointerFixup(m_Texture);
    rsc.PointerFixup(m_Texture2);
    rsc.PointerFixup(m_EnvMap);
    rsc.PointerFixup(m_TexMtx);
    if (m_TexMtx) ObjectFixup(rsc, m_TexMtx);
#endif
}

rmcShader::Pass::Pass()
    : m_State(nullptr), m_Flags(0), m_Blend(0), m_Pad(0), m_NextPass(nullptr)
{
}

rmcShader::Pass::Pass(datResource &rsc)
{
#if defined(__WIN32PC)
    u32 start = rsc.Tell();
    rsc.PointerFixup(m_State);
    m_Flags = rsc.GetU8();
    m_Blend = rsc.GetU8();
    m_Pad = rsc.GetU16();
    ObjectFixup(rsc, m_FirstStage);
    ObjectFixup(rsc, m_NextPass);
    u32 consumed = rsc.Tell() - start;
    (void)consumed;
#else
    rsc.PointerFixup(m_State);
    rsc.PointerFixup(m_NextPass);
    if (m_NextPass) ObjectFixup(rsc, m_NextPass);
#endif
}

rmcShader::TextureMatrix::TextureMatrix(datResource &rsc)
{
#if defined(__WIN32PC)
    u32 start = rsc.Tell();
    rsc.PointerFixup(m_ScaleS);
    rsc.PointerFixup(m_ScaleT);
    rsc.PointerFixup(m_TransS);
    rsc.PointerFixup(m_TransT);
    rsc.PointerFixup(m_Rot);
    u32 consumed = rsc.Tell() - start;
    (void)consumed;
#else
    rsc.PointerFixup(m_ScaleS);
    rsc.PointerFixup(m_ScaleT);
    rsc.PointerFixup(m_TransS);
    rsc.PointerFixup(m_TransT);
    rsc.PointerFixup(m_Rot);
#endif
}

rmcShader::rmcShader() : m_DrawBucket(0) {
}

rmcShader::~rmcShader() {
}

rmcShader::Pass &rmcShader::GetFirstPass() {
    static Pass s_dummy;
    return s_dummy;
}

// Loads a shader parameters configuration, scanning arguments for texture names.
bool rmcShader::Load(const char* name, char **args1, int arg2, int arg3, const class rmcShaderGroup &group) {
    (void)name; (void)args1; (void)arg2; (void)arg3; (void)group;
    return true;
}

// Storage for registered draw bucket names.
char rmcShader::sm_DrawBucketNames[32][64] = { 0 };

// Stub: SetTemplatePath sets the system directory search path for shader templates (no-op).
void rmcShader::SetTemplatePath(const char* path) {}

// SetDrawBucketName assigns a debug label to a specific render pass bucket.
void rmcShader::SetDrawBucketName(int bucket, const char* name) {
    if (bucket >= 0 && bucket < 32 && name) {
        strncpy(sm_DrawBucketNames[bucket], name, sizeof(sm_DrawBucketNames[bucket]) - 1);
        sm_DrawBucketNames[bucket][sizeof(sm_DrawBucketNames[bucket]) - 1] = '\0';
    }
}

// GetDrawBucketName retrieves the assigned debug label for a specific render pass bucket.
const char* rmcShader::GetDrawBucketName(int bucket) {
    if (bucket >= 0 && bucket < 32 && sm_DrawBucketNames[bucket][0] != '\0') {
        return sm_DrawBucketNames[bucket];
    }
    return nullptr;
}

// Returns the active Pass structure for drawing/binding iteration.

// ---------------------------------------------------------------------------
// rmcShaderGroup (moved from drawable.cpp; the local table semantics are
// load-bearing: GetLocalCount and LookupLocal must agree).
// ---------------------------------------------------------------------------

// The two shader locals every group exposes on this port. Real AGE derived the
// local table from the loaded shader templates; until .shader templates load,
// the set is fixed — but GetLocalCount and LookupLocal MUST agree, or
// entEntityType never allocates the ShaderData that scroni's setShaderLocal
// writes through (LookupLocal returning 1 into a 0-sized array).
static const char *const s_ShaderLocalNames[] = { "occlusion", "opacity" };
static const int s_NumShaderLocals = 2;

rmcShaderGroup::rmcShaderGroup(int count)
    : m_Shaders(nullptr)
    , m_Count(0)
    , m_Capacity(0)
    , m_Locals(nullptr)
{
    if (count > 0) InitCount(count);
}

rmcShaderGroup::~rmcShaderGroup() {
    delete [] m_Shaders;   // the array only — shaders are not owned
}

int rmcShaderGroup::GetLocalCount() const {
    return s_NumShaderLocals;
}

#if __BANK
void rmcShaderGroup::AddWidgets(class bkBank &bank) {
    bank.AddTitle("Shader Group");
}
#endif // __BANK

rmcShaderData *rmcShaderGroup::AllocateLocals() const {
    // Always allocate at least one slot: the game tests the returned pointer
    // for null to decide whether an instance has locals at all, so a group
    // with no locals must still hand back a valid (unused) block.
    int count = GetLocalCount();
    if (count < 1) count = 1;
    rmcShaderData *locals = new rmcShaderData[count];
    for (int i = 0; i < count; ++i) locals[i] = rmcShaderData();
    return locals;
}

void rmcShaderGroup::FreeLocals(rmcShaderData *data) const {
    delete [] data;
}

void rmcShaderGroup::InitCount(int count) {
    delete [] m_Shaders;
    m_Shaders = nullptr;
    m_Count = 0;
    m_Capacity = 0;
    if (count > 0) {
        m_Shaders = new rmcShader*[count];
        for (int i = 0; i < count; ++i) m_Shaders[i] = nullptr;
        m_Capacity = (u16)count;
    }
}

// NULL entries are kept (a "dummy.shadert" slot must keep its index so the
// model's shader indices stay aligned with the type file).
void rmcShaderGroup::Add(class rmcShader *shader) {
    if (!shader && m_Count >= m_Capacity) return;
    if (!shader) { m_Shaders[m_Count++] = nullptr; return; }
    if (m_Count == m_Capacity) {
        int newCap = m_Capacity ? m_Capacity * 2 : 8;
        rmcShader **grown = new rmcShader*[newCap];
        for (int i = 0; i < m_Count; ++i) grown[i] = m_Shaders[i];
        delete [] m_Shaders;
        m_Shaders = grown;
        m_Capacity = (u16)newCap;
    }
    m_Shaders[m_Count++] = shader;
}

rmcShader & rmcShaderGroup::operator[](int index) {
    if (index >= 0 && index < m_Count) {
        return *m_Shaders[index];
    }
    static rmcShader dummy;   // out-of-range fallback so blind indexers can't crash
    return dummy;
}

const rmcShader & rmcShaderGroup::operator[](int index) const {
    if (index >= 0 && index < m_Count) {
        return *m_Shaders[index];
    }
    static rmcShader dummy;
    return dummy;
}

// Looks up the index of a local shader variable by name ("occulation" is the
// original data's spelling of occlusion).
int rmcShaderGroup::LookupLocal(const char* name) const {
    if (!name) return -1;
    if (_stricmp(name, "occulation") == 0) return 0;
    for (int i = 0; i < s_NumShaderLocals; ++i) {
        if (_stricmp(name, s_ShaderLocalNames[i]) == 0) return i;
    }
    return -1;
}

rmcShaderGroup::rmcShaderGroup(datResource &rsc)
    : m_Shaders(nullptr), m_Count(0), m_Capacity(0), m_Locals(nullptr)
{
#if defined(__WIN32PC)
    u32 start = rsc.Tell();
    rsc.GetVTable();
    rsc.PointerFixup(m_Shaders);
    m_Count = rsc.GetU16();
    m_Capacity = rsc.GetU16();
    rsc.PointerFixup(m_Locals);
    u32 consumed = rsc.Tell() - start;
    (void)consumed;
#else
    rsc.PointerFixup(m_Shaders);
    rsc.PointerFixup(m_Locals);
#endif
}

rmcShader *rmcShaderGroup::GetShader(int index) const {
    return (index >= 0 && index < m_Count) ? m_Shaders[index] : nullptr;
}

void rmcShaderGroup::SetShader(int index, rmcShader *shader) {
    if (index >= 0 && index < m_Count) m_Shaders[index] = shader;
}

void rmcShaderGroup::ResourcePageIn(datResource &rsc) {
    (void)rsc;
}

// Resource-built shader: read vtable and bucket from resource
rmcShader::rmcShader(datResource &rsc) : m_DrawBucket(0) {
#if defined(__WIN32PC)
    u32 start = rsc.Tell();
    rsc.GetVTable();
    m_DrawBucket = rsc.GetU32();
    u32 consumed = rsc.Tell() - start;
    (void)consumed;
#endif
}

rmcShaderInstance::rmcShaderInstance() : rmcShader() {}
rmcShaderInstance::rmcShaderInstance(class datResource &rsc) : rmcShader(rsc) {}
rmcShaderInstance::~rmcShaderInstance() {}


bool rmcShader::Load(const char *name, const char * const *args1, int arg2, int arg3, const rmcShaderGroup &group) {
    return Load(name, const_cast<char **>(args1), arg2, arg3, group);
}

bool rmcShader::Load(const char *name, const char **args1, int arg2, int arg3, const rmcShaderGroup &group) {
    return Load(name, const_cast<char **>(args1), arg2, arg3, group);
}

static rmcShaderFactory *s_ShaderFactoryStack[8];
static int s_ShaderFactoryDepth = 0;

void rmcShaderFactory::InitClass() {
    if (!sm_Instance) {
        sm_Instance = CreateStandardShaderFactory();
    }
}

void rmcShaderFactory::ShutdownClass() {
    s_ShaderFactoryDepth = 0;
    sm_Instance = nullptr;
}

rmcShaderFactory *rmcShaderFactory::CreateStandardShaderFactory() {
    static rmcShaderFactory standardFactory;
    return &standardFactory;
}

void rmcShaderFactory::PushInstance(rmcShaderFactory *inst) {
    if (!inst) return;
    if (s_ShaderFactoryDepth < 8) {
        s_ShaderFactoryStack[s_ShaderFactoryDepth++] = sm_Instance;
    }
    sm_Instance = inst;
}

void rmcShaderFactory::PopInstance() {
    if (s_ShaderFactoryDepth > 0) {
        sm_Instance = s_ShaderFactoryStack[--s_ShaderFactoryDepth];
    }
}

rmcShader *rmcShaderFactory::Create(const char *filename, int argCount, const char **args) {
    return nullptr;
}

rmcShaderGroup *rmcShaderFactory::CreateGroup(int count) {
    return new rmcShaderGroup();
}

void rmcShaderFactory::ResourcePageIn(datResource &rsc, rmcShader **shaders, int count) {
    if (!shaders || count <= 0) return;
    for (int i = 0; i < count; ++i) {
        rsc.PointerFixup(shaders[i]);
    }
}

void rmcShaderFactory::ResourcePageIn(datResource &rsc, rmcShaderGroup *groups) {
    if (!groups) return;
    groups->ResourcePageIn(rsc);
}

// ---------------------------------------------------------------------------
// rmcShaderTemplate - the level's numbered template slots
// ---------------------------------------------------------------------------

namespace {

struct ShaderTemplateSlot {
    char Name[64];
    const char *Text;
};

ShaderTemplateSlot s_Templates[rmcShaderTemplate::MAX_TEMPLATES];

bool ValidTemplateId(int id) {
    return id >= 0 && id < rmcShaderTemplate::MAX_TEMPLATES;
}

} // anonymous namespace

void rmcShaderTemplate::Load(int id, const char *name) {
    if (!ValidTemplateId(id) || !name) return;

    // Reloading a slot replaces what was there.
    if (s_Templates[id].Text) Unload(id);

    // The cache owns the text and reads the file once; asking for it here is
    // what makes the template resident for as long as the slot holds it.
    s_Templates[id].Text = rmcShadertCache.Get(name);
    strncpy(s_Templates[id].Name, name, sizeof(s_Templates[id].Name) - 1);
    s_Templates[id].Name[sizeof(s_Templates[id].Name) - 1] = 0;

    if (!s_Templates[id].Text)
        Warningf("rmcShaderTemplate::Load: '%s' not found (slot %d)", name, id);
}

void rmcShaderTemplate::Unload(int id) {
    if (!ValidTemplateId(id)) return;
    // The text belongs to the cache, so releasing a slot only drops the
    // reference; rmcShadertCache.Kill() is what frees the text itself.
    s_Templates[id].Text = 0;
    s_Templates[id].Name[0] = 0;
}

const char *rmcShaderTemplate::Get(int id) {
    return ValidTemplateId(id) ? s_Templates[id].Text : 0;
}

const char *rmcShaderTemplate::GetName(int id) {
    return (ValidTemplateId(id) && s_Templates[id].Name[0]) ? s_Templates[id].Name : 0;
}
