#include "imx51_gpu2d_pixel_blend.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include "imx51_gpu2d_blend.h"
#include "imx51_gpu2d_rasterizer.h"
#include "imx51_id.h"

#include <algorithm>
#include <cstdint>

namespace cerfcore {

REGISTER_SERVICE(Imx51Gpu2dPixelBlend);

using namespace imx51_g2d_blend;

bool Imx51Gpu2dPixelBlend::ShouldRegister() {
    return emu_.Get<BoardContext>().GetSocId() == SocId::Imx51;
}

void Imx51Gpu2dPixelBlend::HaltBlend(const char* why, uint32_t prog) const {
    emu_.Get<Fatal>().Die("[GPU2D-RAST] %s prog=0x%08X - not modeled", why, prog);
}

bool Imx51Gpu2dPixelBlend::BlendProgRefsDest(uint32_t prog) {
    for (uint32_t i = 0; i < 4u; ++i)
        if (!Const(prog, i) && Src(prog, i) == kSrcDest) return true;
    return false;
}

void Imx51Gpu2dPixelBlend::ValidateBlendProg(uint32_t prog) const {
    if (Operation(prog) != 0u)  /* G2D_BLENDOP_ADD (vgenums_z160.h:181-186) */
        HaltBlend("blend OPERATION SUB/MIN/MAX", prog);
    if (Dst(prog, 0) != 1u || Dst(prog, 1) != 0u || Dst(prog, 2) != 0u)
        HaltBlend("blend multi-TEMP routing", prog);
    for (uint32_t i = 0; i < 4u; ++i) {
        if (!Const(prog, i) && Src(prog, i) > kSrcDest)
            HaltBlend("blend IMAGE/TEMP operand", prog);
    }
}

uint32_t Imx51Gpu2dPixelBlend::BlendPixel(const Gpu2dFillTarget& t, uint32_t src, uint32_t dst,
                                          float cov) const {
    if (cov < 1.0f) {
        auto cv = [cov](uint32_t ch) {
            return static_cast<uint32_t>(static_cast<float>(ch) * cov + 0.5f);
        };
        src = (cv(src >> 24) << 24) | (cv((src >> 16) & 0xFFu) << 16)
            | (cv((src >> 8) & 0xFFu) << 8) | cv(src & 0xFFu);
    }
    if (t.premult_dst)
        dst = PremultArgb(dst);
    const uint32_t inputs[] = {0u, src, dst};
    auto chan = [&](uint32_t prog, uint32_t shift) -> uint32_t {
        auto operand = [&](uint32_t i) -> uint32_t {
            return Operand(prog, i, shift, inputs, t.blend_const);
        };
        const uint32_t sum = Mul(operand(0), operand(1)) + Mul(operand(2), operand(3));
        return std::min(sum, 255u);
    };
    uint32_t a, r, g, b;
    if (t.color_transform) {
        const uint32_t alpha[] = {t.prog_a, t.prog_a1}, color[] = {t.prog_c, t.prog_c1};
        const uint32_t out = AddProgram(alpha, 2u, color, 2u, t.blend_const, src, dst);
        a = out >> 24;
        r = (out >> 16) & 0xFFu;
        g = (out >> 8) & 0xFFu;
        b = out & 0xFFu;
    } else {
        a = chan(t.prog_a, 24u);
        r = chan(t.prog_c, 16u);
        g = chan(t.prog_c, 8u);
        b = chan(t.prog_c, 0u);
    }
    if (t.oo_alpha && a != 255u) {
        if (a == 0u) {
            if (r | g | b)
                HaltBlend("OOALPHA divide on zero-alpha nonzero color", t.prog_c);
        } else {
            r = UnpremultChannel(r, a);
            g = UnpremultChannel(g, a);
            b = UnpremultChannel(b, a);
        }
    }
    return (a << 24) | (r << 16) | (g << 8) | b;
}

}
