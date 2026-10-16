#define NOMINMAX
#include "imx51_gpu2d_gradw_sampler.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/boards/board_context.h>
#include "imx51_id.h"
#include <cerfcore/cpu/emulated_memory.h>
#include "imx51_gpu2d_blend.h"

#include <algorithm>
#include <cmath>

namespace cerfcore {

namespace {
/* Per-channel linear blend of two ARGB8888 texels by frac in [0,1] (round). */
uint32_t Lerp(uint32_t a, uint32_t b, float f) {
    uint32_t out = 0;
    for (uint32_t sh = 0; sh < 32u; sh += 8u) {
        const float ca = static_cast<float>((a >> sh) & 0xFFu);
        const float cb = static_cast<float>((b >> sh) & 0xFFu);
        out |= (static_cast<uint32_t>(ca + (cb - ca) * f + 0.5f) & 0xFFu) << sh;
    }
    return out;
}
enum { kOpDot = 0, kOpRcp = 1, kOpSqrtMul = 2, kOpSqrtAdd = 3 };
/* REPEAT tiling (G2D_WRAP_REPEAT): wrap a texel coord into [0,n), n>=1. */
int32_t WrapRepeat(int32_t v, int32_t n) {
    v %= n;
    return v < 0 ? v + n : v;
}
}  // namespace

REGISTER_SERVICE(Imx51Gpu2dGradwSampler);

bool Imx51Gpu2dGradwSampler::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Imx51;
}

/* GRADW ISA (vgenums_z160.h:188-193 G2D_GRAD_OP; vgregs_z160.h:928-937 INST
   SRC_E:5/SRC_D:5/SRC_C:5/SRC_B:5/SRC_A:5/DST:4/OPCODE:2). DOT = A*B+C*D+E;
   SQRTMUL = sqrt(A)*B+C*D+E - NOT sqrt(A*B): INST3 SQRTMUL(dist^2, C6=1/radius)
   must equal the OpenVG radial parameter dist/r, and C6=1/r forces the sqrt(A)*B form. */
void Imx51Gpu2dGradwSampler::EvalProgram(const Gpu2dGradwPaint& p, int32_t px, int32_t py,
                                         float& outx, float& outy) const {
    float r[32] = {0.0f};
    r[0] = static_cast<float>(px);   /* GRADREG_X */
    r[1] = static_cast<float>(py);   /* GRADREG_Y */
    for (uint32_t i = 0; i < 12u; ++i) r[16u + i] = p.cnst[i];  /* C0-C11 */
    r[29] = 1.0f;    /* ONE  (ZERO=r[28] already 0) */
    r[30] = -1.0f;   /* MINUSONE */

    auto val = [&](uint32_t idx) -> float {
        if (idx <= 9u || (idx >= 16u && idx <= 30u)) return r[idx];
        LOG(Caution, "[GPU2D-GRADW] ISA reads un-modeled GRADREG %u\n", idx);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    };

    const uint32_t n = std::min(p.ninst, 8u);
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t w   = p.inst[i];
        const float    ve  = val(w & 0x1Fu),        vd = val((w >> 5) & 0x1Fu);
        const float    vc  = val((w >> 10) & 0x1Fu), vb = val((w >> 15) & 0x1Fu);
        const float    va  = val((w >> 20) & 0x1Fu);
        const uint32_t dst = (w >> 25) & 0xFu, op = (w >> 29) & 0x3u;
        float res;
        if (op == kOpDot)          res = va * vb + vc * vd + ve;
        else if (op == kOpSqrtMul) res = std::sqrt(va) * vb + vc * vd + ve;
        else {
            LOG(Caution, "[GPU2D-GRADW] ISA opcode %u not modeled (INST%u=0x%08X)\n", op, i, w);
            CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
        }
        if (dst > 9u) {  /* writable: X/Y-as-temp, temps 2-7, OUTX(8)/OUTY(9) */
            LOG(Caution, "[GPU2D-GRADW] ISA writes un-modeled GRADREG %u\n", dst);
            CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
        }
        r[dst] = res;
    }
    outx = r[8];   /* GRADREG_OUTX */
    outy = r[9];   /* GRADREG_OUTY */
}

uint32_t Imx51Gpu2dGradwSampler::Fetch(const Gpu2dGradwPaint& p, int32_t tx, int32_t ty) const {
    /* WRAP=0 = clamp to edge (VG_COLOR_RAMP_SPREAD_PAD); WRAP=1 = REPEAT (tile modulo). */
    tx = p.wrap_u ? WrapRepeat(tx, p.texw) : std::clamp(tx, 0, p.texw - 1);
    ty = p.wrap_v ? WrapRepeat(ty, p.texh) : std::clamp(ty, 0, p.texh - 1);
    const uint32_t pa = p.texbase + (static_cast<uint32_t>(ty) * p.tstride
                                     + static_cast<uint32_t>(tx)) * 4u;
    const uint8_t* hp = emu_.Get<EmulatedMemory>().TryTranslate(pa);
    if (!hp) {
        LOG(Caution, "[GPU2D-GRADW] texel unbacked pa=0x%08X (tx=%d ty=%d)\n", pa, tx, ty);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    return *reinterpret_cast<const uint32_t*>(hp);  /* fmt7 ARGB8888 */
}

uint32_t Imx51Gpu2dGradwSampler::Sample(const Gpu2dGradwPaint& p,
                                        int32_t px, int32_t py) const {
    float s, t;
    EvalProgram(p, px, py, s, t);
    uint32_t texel;
    if (p.bilinear) {
        /* Half-texel centres (GPU texel-centre convention), clamp at edges. */
        const float su = s * p.texw - 0.5f, tv = t * p.texh - 0.5f;
        const int32_t x0 = static_cast<int32_t>(std::floor(su));
        const int32_t y0 = static_cast<int32_t>(std::floor(tv));
        const float fxr = su - static_cast<float>(x0), fyr = tv - static_cast<float>(y0);
        texel = Lerp(Lerp(Fetch(p, x0, y0), Fetch(p, x0 + 1, y0), fxr),
                     Lerp(Fetch(p, x0, y0 + 1), Fetch(p, x0 + 1, y0 + 1), fxr), fyr);
    } else {
        texel = Fetch(p, static_cast<int32_t>(std::floor(s * p.texw)),
                      static_cast<int32_t>(std::floor(t * p.texh)));
    }
    /* openvg_spec.pdf 9.3.3: a premultiplied ramp is interpolated in premult form
       (texel already premult); a non-premult ramp is interpolated independently,
       then premultiplied here for the g12's premultiplied blend ALU. */
    return p.premultiply ? texel : imx51_g2d_blend::PremultArgb(texel);
}

}
