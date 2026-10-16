#pragma once

#include <cstdint>

/* G2D blend micro-op word field decoders (REG_G2D_BLEND_A0/C0, vgregs_z160.h:366-387;
   A0-A3 / C0-C7 share this layout). */
namespace imx51_g2d_blend {

constexpr uint32_t Operation(uint32_t w)          { return w & 3u; }               /* vgenums_z160.h:181-186: ADD=0 */
constexpr uint32_t Dst(uint32_t w, uint32_t i)    { return (w >> (2u + 2u * i)) & 3u; }   /* DST_A/B/C: IGNORE=0/TEMP0-2=1-3 */
constexpr uint32_t Src(uint32_t w, uint32_t i)    { return (w >> (16u + 3u * i)) & 7u; }  /* opA..D source selector */
constexpr bool     Ar(uint32_t w, uint32_t i)     { return ((w >> (8u + i)) & 1u) != 0u; }  /* alpha-replicate: read the alpha channel */
constexpr bool     Inv(uint32_t w, uint32_t i)    { return ((w >> (12u + i)) & 1u) != 0u; } /* 1 - x */
constexpr bool     Const(uint32_t w, uint32_t i)  { return ((w >> (28u + i)) & 1u) != 0u; }

/* G2D_BLEND_SRC operand selectors (vgenums_z160.h:195-203). */
constexpr uint32_t kSrcZero   = 0u;
constexpr uint32_t kSrcSource = 1u;  /* paint with coverage */
constexpr uint32_t kSrcDest   = 2u;
constexpr uint32_t kSrcImage  = 3u;  /* second texture */
constexpr uint32_t kSrcTemp0  = 4u;
constexpr uint32_t kSrcTemp1  = 5u;
constexpr uint32_t kSrcTemp2  = 6u;

/* G2D_BLEND_DST routing (vgenums_z160.h:205-209). */
constexpr uint32_t kDstIgnore = 0u;

/* One 8-bit channel product with the HW's round-to-nearest /255. */
constexpr uint32_t Mul(uint32_t a, uint32_t b) { return (a * b + 127u) / 255u; }

/* NXP Z160 REG_G2D_BLEND_A0/C0: CONST selects G2D_CONST[SRC] instead
   of a pixel input. sync_2 libOpenVG.dll 0x41C57D14/0x41C57F78 encode
   this same selector, followed by alpha replication and inversion. */
inline uint32_t Operand(uint32_t prog, uint32_t i, uint32_t shift,
                        const uint32_t* inputs, const uint32_t* constants) {
    const uint32_t pixel = Const(prog, i) ? constants[Src(prog, i)] : inputs[Src(prog, i)];
    const uint32_t v = (pixel >> (Ar(prog, i) ? 24u : shift)) & 0xFFu;
    return Inv(prog, i) ? 255u - v : v;
}

/* ADD programs, validated by the dispatchers. Both pipes read the previous
   pass's TEMP values: sync_2 libOpenVG.dll 0x41C58230 writes TEMP1 RGBA,
   and 0x41C60610 emits a following color pass that reads TEMP1.a. */
inline uint32_t AddProgram(const uint32_t* alpha, uint32_t alpha_passes,
                           const uint32_t* color, uint32_t color_passes,
                           const uint32_t* constants, uint32_t src, uint32_t dst) {
    uint32_t temp[3] = {};
    for (uint32_t p = 0; p < color_passes || p < alpha_passes; ++p) {
        const uint32_t inputs[] = {0u, src, dst, 0xFFFFFFFFu, temp[0], temp[1], temp[2]};
        auto run = [&](uint32_t prog, uint32_t shift) {
            const uint32_t p1 = Mul(Operand(prog, 0, shift, inputs, constants),
                                     Operand(prog, 1, shift, inputs, constants));
            const uint32_t p2 = Mul(Operand(prog, 2, shift, inputs, constants),
                                     Operand(prog, 3, shift, inputs, constants));
            auto put = [&](uint32_t d, uint32_t value) {
                if (d == kDstIgnore) return;
                if (value > 255u) value = 255u;
                temp[d - 1u] = (temp[d - 1u] & ~(0xFFu << shift)) | (value << shift);
            };
            put(Dst(prog, 0), p1 + p2);
            put(Dst(prog, 1), p1);
            put(Dst(prog, 2), p2);
        };
        if (p < color_passes) {
            run(color[p], 16u);
            run(color[p], 8u);
            run(color[p], 0u);
        }
        if (p < alpha_passes) run(alpha[p], 24u);
    }
    return temp[0];
}

/* Per-channel product of two PREMULTIPLIED ARGB8888 pixels (all 4 channels incl.
   alpha). texel(premult)_C * COLOR(premult)_C == imageA*paintA*Cimage*Cpaint = the
   OpenVG image-mode premultiplied source (openvg_spec.pdf VG_DRAW_IMAGE_STENCIL/
   MULTIPLY, "multiplied channel-by-channel"); straight-alpha inputs give wrong glyphs. */
constexpr uint32_t MulArgb(uint32_t x, uint32_t y) {
    return (Mul(x >> 24, y >> 24) << 24)
         | (Mul((x >> 16) & 0xFFu, (y >> 16) & 0xFFu) << 16)
         | (Mul((x >> 8) & 0xFFu, (y >> 8) & 0xFFu) << 8)
         | Mul(x & 0xFFu, y & 0xFFu);
}

/* Premultiply an ARGB8888 pixel's RGB by its own alpha (straight-alpha DEST going
   into the premultiplied blend ALU under ALPHABLEND.PREMULTIPLYDST). */
constexpr uint32_t PremultArgb(uint32_t argb) {
    const uint32_t a = argb >> 24;
    return (a << 24) | (Mul((argb >> 16) & 0xFFu, a) << 16)
         | (Mul((argb >> 8) & 0xFFu, a) << 8) | Mul(argb & 0xFFu, a);
}

/* One un-premultiply channel divide (BLENDERCFG.OOALPHA result -> straight alpha). */
constexpr uint32_t UnpremultChannel(uint32_t ch, uint32_t a) {
    return (ch * 255u + a / 2u) / a > 255u ? 255u : (ch * 255u + a / 2u) / a;
}

}  // namespace imx51_g2d_blend
