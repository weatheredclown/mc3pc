#ifndef RMCORE_SHADERBASIC_H
#define RMCORE_SHADERBASIC_H

#include "core/output.h"
#include "rmcore/shader.h"

class rmcShaderBasic : public rmcShader {
public:
    using rmcShader::Load;

    rmcShaderBasic();
    rmcShaderBasic(datResource &rsc);
    virtual ~rmcShaderBasic() {}

    virtual void Draw(const rmcModel &model, const rmcShaderData *data, const rmcGeometry &geom, int lod) const override { Quitf("rmcShaderBasic::Draw - not implemented"); }
    virtual void DrawCpv(const rmcModel &model, const rmcShaderData *data, const rmcGeometry &geom, int lod, const rmcGeometryCpv &cpv) const override { Quitf("rmcShaderBasic::DrawCpv - not implemented"); }
    virtual void DrawSkinned(const rmcModel &model, const rmcShaderData *data, const rmcGeometry &geom, const Matrix34 *mtxs, int mtxCount, int lod) const override { Quitf("rmcShaderBasic::DrawSkinned - not implemented"); }
    virtual void Bind(const rmcShaderData *locals, const rmcShaderData *globals, int pass = 0) const override { Quitf("rmcShaderBasic::Bind - not implemented"); }
    virtual void RestoreStates() const override { Quitf("rmcShaderBasic::RestoreStates - not implemented"); }

public:
    class rmcTexture *m_BaseTexture; // +0x8 (4 bytes)
};

#if !defined(_WIN64)
static_assert(sizeof(rmcShaderBasic) == 12, "rmcShaderBasic size mismatch");
#endif

#endif // RMCORE_SHADERBASIC_H
