#include "rmcore/texturedefault.h"
#include "gfx/texture.h"

rmcTexture *rmcTextureFactoryDefault::CreateDefault(const char *filename) {
    if (!filename || filename[0] == '\0' || _stricmp(filename, "none") == 0) {
        return rmcTexture::None;
    }

    bool found = false;
    rmcTexture *ref = rmcTextureFactory::LookupTextureReference(filename, found);
    if (found && ref) {
        return new rmcTextureReference(filename, ref);
    }

    gfxTexture *tex = gfxGetTexture(filename);
    if (!tex) {
        return rmcTexture::None;
    }
    return new rmcTextureGfx(tex);
}

rmcTexture *rmcTextureFactoryDefault::Create(const char *filename) {
    return CreateDefault(filename);
}

void rmcTextureFactoryPS2::ResourcePageIn(datResource &res, rmcTexture **texList, int count) {
    if (!texList || count <= 0) return;
    for (int i = 0; i < count; ++i) {
        res.PointerFixup(texList[i]);
    }
}

void rmcTextureFactoryGfx::ResourcePageIn(datResource &res, rmcTexture **texList, int count) {
    if (!texList || count <= 0) return;
    for (int i = 0; i < count; ++i) {
        res.PointerFixup(texList[i]);
    }
}
