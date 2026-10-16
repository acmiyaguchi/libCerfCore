#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

/* GRADW paint input decoded from the g12 register file. The paint colour at a
   dest pixel is the fmt7 texel at the (OUTX,OUTY) the GRADW ISA program computes. */
struct Gpu2dGradwPaint {
    uint32_t texbase;     /* GRADW TEXBASE (texel-atlas / ramp base; gpuaddr==physical) */
    int32_t  texw, texh;  /* GRADW TEXSIZE WIDTH[10:0] / HEIGHT[21:11] (texels) */
    uint32_t tstride;     /* GRADW TEXCFG.STRIDE + 1 (texels per row) */
    bool     bilinear;    /* TEXCFG.BILIN: 4-tap linear filter vs nearest */
    bool     premultiply; /* TEXCFG.PREMULTIPLY: texel stored premultiplied (else premult the result) */
    bool     wrap_u = false;  /* TEXCFG.WRAPU: false=CLAMP(PAD), true=REPEAT (tile modulo texw) */
    bool     wrap_v = false;  /* TEXCFG.WRAPV: false=CLAMP(PAD), true=REPEAT (tile modulo texh) */
    uint32_t inst[8];     /* GRADW ISA words INST0..INST(ninst-1) */
    uint32_t ninst;       /* GRADIENT.INSTRUCTIONS(2) + 1 */
    float    cnst[12];    /* coefficient registers C0-C11 (g12-half-float-unpacked) */
};

class Imx51Gpu2dGradwSampler : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    /* Run the program at BASE0-local pixel (px,py) -> (OUTX,OUTY) texcoords, then
       fetch the fmt7 premultiplied texel (bilinear/nearest, edge-clamp or REPEAT). */
    uint32_t Sample(const Gpu2dGradwPaint& p, int32_t px, int32_t py) const;

private:
    void EvalProgram(const Gpu2dGradwPaint& p, int32_t px, int32_t py,
                     float& outx, float& outy) const;
    uint32_t Fetch(const Gpu2dGradwPaint& p, int32_t tx, int32_t ty) const;  /* fmt7 texel, clamp/repeat */
};

}
