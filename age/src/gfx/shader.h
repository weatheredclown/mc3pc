////////////////////////////////////////
// shader.h
////////////////////////////////////////

#ifndef GFX_SHADER_H
#define GFX_SHADER_H

#include "gfx/misc.h"

class gfxShader {
private:
    class gfxMaterial m_Material;
    class gfxTexture* m_Texture;
public:
    gfxShader() : m_Texture(nullptr) {}
    class gfxMaterial* GetMaterial() { return &m_Material; }
    const class gfxMaterial* GetMaterial() const { return &m_Material; }
    class gfxTexture* GetTexture() const { return m_Texture; }
    void SetTexture(class gfxTexture* tex) { m_Texture = tex; }
    void SetMaterial(const class gfxMaterial* mat) {
        if (mat) m_Material = *mat;
    }
};

class gfxShaderFactory {
public:
    virtual ~gfxShaderFactory() {}
};

#endif // GFX_SHADER_H
