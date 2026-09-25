#ifndef RMCORE_TEXTUREDEFAULT_H
#define RMCORE_TEXTUREDEFAULT_H

#include "core/output.h"
#include "rmcore/texture.h"

class rmcTextureFactoryDefault : public rmcTextureFactory {
public:
    rmcTextureFactoryDefault() {}
    virtual ~rmcTextureFactoryDefault() {}

    virtual rmcTexture *Create(const char *filename) override;
    static rmcTexture *CreateDefault(const char *filename);
};

class rmcTextureFactoryPC : public rmcTextureFactoryDefault {
public:
    static rmcTexture *Create(const char *filename) {
        return rmcTextureFactoryDefault::CreateDefault(filename);
    }
};

class rmcTextureFactoryXbox : public rmcTextureFactoryDefault {
public:
    static rmcTexture *Create(const char *filename) {
        return rmcTextureFactoryDefault::CreateDefault(filename);
    }
};

class rmcTextureFactoryPS2 : public rmcTextureFactoryDefault {
public:
    static rmcTexture *Create(const char *filename) {
        return rmcTextureFactoryDefault::CreateDefault(filename);
    }
    static void ResourcePageIn(class datResource &res, rmcTexture **texList, int count);
    static void FlushVram() { Quitf("rmcTextureFactoryPS2::FlushVram - not implemented"); }
};

class rmcTextureFactoryGfx : public rmcTextureFactoryDefault {
public:
    static rmcTexture *Create(const char *filename) {
        return rmcTextureFactoryDefault::CreateDefault(filename);
    }
    static void ResourcePageIn(class datResource &res, rmcTexture **texList, int count);
    static void FlushVram() { Quitf("rmcTextureFactoryGfx::FlushVram - not implemented"); }
};

#endif // RMCORE_TEXTUREDEFAULT_H
