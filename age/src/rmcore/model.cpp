////////////////////////////////////////
// rmcore/model.cpp
//
// rmcModel statics: the CPV (colour-per-vertex) stream resolver.
////////////////////////////////////////

#include "rmcore/model.h"
#include "data/assetcfg.h"
#include "core/stream.h"

#include <cstring>

// Indexed CPV files (.icpv) carry a palette index per vertex instead of a
// colour; the city loader sets this from the cpv mode it is loading.
bool rmcCpvIndexed = false;

// Default resolver: "<model>.cpv" (or .icpv) next to the model through ASSET.
Stream *__default_open_cpv_file(const char *basename) {
    if (!basename) return nullptr;
    char base[256];
    strncpy(base, basename, sizeof(base) - 1); base[sizeof(base) - 1] = 0;
    if (char *dot = strrchr(base, '.')) *dot = 0;
    const char *ext = rmcCpvIndexed ? "icpv" : "cpv";
    if (!ASSET.Exists(base, ext)) return nullptr;
    return ASSET.Open(base, ext);
}

Stream *(*rmcModel::OpenCpvFile)(const char *basename) = __default_open_cpv_file;

int rmcModel::sm_PositionFractionalBits = 0;

rmcModel *rmcModel::Create(const char *name)
{
    if (!name || !name[0]) return nullptr;
    rmcModel *m = new rmcModel;
    if (!m->Load(name)) { delete m; return nullptr; }
    return m;
}

rmcModel *rmcModel::Create(const char *type, const char *name, int /*flags*/, int /*fvf*/)
{
    (void)type;
    return Create(name);
}

// Exporter/loader switches the game pokes from its level and car loaders.
bool rmcEnableSwizzle = false;
bool rmcIgnoreNormals = false;
int rmcShaderGroupIndex = 0;
rmcModelFactory *rmcModelFactory::sm_Instance = nullptr;

static rmcModelFactory *s_ModelFactoryStack[8];
static int s_ModelFactoryTop = 0;

void rmcModelFactory::PushInstance(rmcModelFactory *inst)
{
    if (s_ModelFactoryTop < (int)(sizeof(s_ModelFactoryStack) / sizeof(s_ModelFactoryStack[0]))) {
        s_ModelFactoryStack[s_ModelFactoryTop++] = sm_Instance;
    }
    if (inst) sm_Instance = inst;
}

void rmcModelFactory::PushInstance(rmcModelFactory &inst)
{
    PushInstance(&inst);
}

void rmcModelFactory::PopInstance()
{
    if (s_ModelFactoryTop > 0) {
        sm_Instance = s_ModelFactoryStack[--s_ModelFactoryTop];
    }
}

