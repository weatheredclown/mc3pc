#ifndef RMCORE_SHADERCOMPLEX_H
#define RMCORE_SHADERCOMPLEX_H

#include "core/output.h"
#include "rmcore/shader.h"

class rmcShaderComplex : public rmcShader {
public:
    using rmcShader::Load;

    rmcShaderComplex();
    rmcShaderComplex(datResource &rsc);
    virtual ~rmcShaderComplex() {}

    virtual void Bind(const rmcShaderData *locals, const rmcShaderData *globals, int pass = 0) const override { Quitf("rmcShaderComplex::Bind - not implemented"); }
    virtual void Draw(const rmcModel &model, const rmcShaderData *data, const rmcGeometry &geom, int lod) const override { Quitf("rmcShaderComplex::Draw - not implemented"); }
    virtual Pass &GetFirstPass() override { return m_FirstPass; }
    const char *GetName() const { return m_Name; }

public:
    Pass m_FirstPass;        // +0x8 (32 bytes: 0x8..0x27)
    const char *m_Name;      // +0x28 (4 bytes)
    void *m_Unk_0x2c;        // +0x2c (4 bytes)
};

#if !defined(_WIN64)
static_assert(sizeof(rmcShaderComplex) == 48, "rmcShaderComplex size mismatch");
#endif

#endif // RMCORE_SHADERCOMPLEX_H

