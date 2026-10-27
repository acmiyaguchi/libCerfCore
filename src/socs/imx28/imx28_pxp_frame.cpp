#include "imx28_pxp_frame.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/cpu/emulated_memory.h>
#include "imx28_pxp_regs.h"

namespace cerfcore {

using namespace imx28_pxp;

namespace {

constexpr uint32_t kCtrlUnmodelled = kCtrlScale | kCtrlInPlace | kCtrlInterlaced | kCtrlHflip |
                                     kCtrlVflip;

/* Sections 34.4.9, 34.4.11: XBASE [31:24], YBASE [23:16], WIDTH [15:8], HEIGHT [7:0]. */
struct BlockRect {
    uint32_t xbase;
    uint32_t ybase;
    uint32_t width;
    uint32_t height;
};

BlockRect Fields(uint32_t v) {
    return {v >> 24, (v >> 16) & 0xFFu, (v >> 8) & 0xFFu, v & 0xFFu};
}

/* Section 34.2.1: 16-bit pixels take the most significant bits of the 24-bit value. */
uint16_t To565(uint32_t rgb) {
    return static_cast<uint16_t>(((rgb >> 8) & 0xF800u) | ((rgb >> 5) & 0x07E0u) |
                                 ((rgb >> 3) & 0x001Fu));
}

/* Section 34.4.14 to 34.4.16: two's complement coefficient fields. */
int32_t SignExtend(uint32_t v, uint32_t bits) {
    const uint32_t m = 1u << (bits - 1u);
    v &= (1u << bits) - 1u;
    return static_cast<int32_t>(v ^ m) - static_cast<int32_t>(m);
}

uint8_t Saturate(int32_t v) { return static_cast<uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v); }

/* Sections 34.2.2 Figure 34-6, 34.2.3: bilinear sample at a 1/4096 pixel position of a
   plane read linearly from its base, so a column past the width wraps to the next line. */
uint32_t Bilinear(const std::vector<uint8_t>& plane, uint32_t stride, uint32_t fx, uint32_t fy) {
    const uint32_t ax = fx & 0xFFFu;
    const uint32_t ay = fy & 0xFFFu;
    const uint8_t* r0 = plane.data() + static_cast<size_t>(fy >> 12) * stride + (fx >> 12);
    const uint8_t* r1 = r0 + stride;
    const uint32_t top = r0[0] * (4096u - ax) + r0[1] * ax;
    const uint32_t bot = r1[0] * (4096u - ax) + r1[1] * ax;
    return (top * (4096u - ay) + bot * ay + (1u << 23)) >> 24;
}

}  /* namespace */

void Imx28PxpFrame::Die(const char* what, uint32_t value) const {
    emu_.Get<Fatal>().Die("Imx28 PXP: %s (value %08X)", what, value);
}

void Imx28PxpFrame::Render(const uint32_t* regs) {
    const uint32_t ctrl = regs[Slot(kCtrl)];
    const uint32_t s0_fmt = (ctrl & kCtrlS0Format) >> 12;
    const uint32_t out_fmt = (ctrl & kCtrlOutFormat) >> 4;
    const bool     yuv = s0_fmt == kFormatYuv420;
    /* Section 34.4.1 SCALE: only YUV/YCbCr images may be scaled. */
    const uint32_t unmodelled = yuv ? kCtrlUnmodelled & ~kCtrlScale : kCtrlUnmodelled;
    if ((ctrl & unmodelled) != 0u || (s0_fmt != kFormatRgb565 && !yuv) ||
        out_fmt != kFormatRgb565) {
        return;
    }
    if (!yuv && (ctrl & kCtrlCrop) == 0u) Die("CTRL CROP clear", ctrl);
    /* Sections 34.4.18, 34.4.19: LOW FFFFFFh with HIGH 0 disables S0 colorkeying. */
    if (regs[Slot(kS0ColorkeyLo)] != 0x00FFFFFFu || regs[Slot(kS0ColorkeyHi)] != 0u)
        Die("S0 colorkey enabled", regs[Slot(kS0ColorkeyHi)]);
    for (uint32_t n = 0; n < kOverlays; ++n) {
        const uint32_t param = regs[Slot(kOl0 + n * kOlStride + kOlParam)];
        if ((param & 1u) != 0u) Die("overlay ENABLE", param);
    }
    /* Section 34.4.5: WIDTH [23:12], HEIGHT [11:0] in pixels, after rotation. */
    const uint32_t outsize = regs[Slot(kOutsize)];
    const uint32_t out_w   = (outsize >> 12) & 0xFFFu;
    const uint32_t out_h   = outsize & 0xFFFu;
    const uint32_t rotate  = (ctrl >> kCtrlRotateShift) & 0x3u;
    if (out_w == 0u || out_h == 0u) Die("OUTSIZE zero", outsize);
    if ((rotate & 1u) != 0u && out_w != out_h) Die("90 or 270 rotation of non-square OUTSIZE",
                                                   outsize);
    if (yuv) FillPreYuv420(regs, out_w, out_h);
    else     FillPre(regs, out_w, out_h);
    Rotate(rotate, out_w, out_h, out_w, out_h);
    const uint32_t bytes = out_w * out_h * 2u;
    if (emu_.Get<EmulatedMemory>().TryCopyIn(regs[Slot(kOutbuf)],
                                             reinterpret_cast<const uint8_t*>(out_.data()),
                                             bytes) != bytes) {
        Die("OUTBUF write outside RAM", regs[Slot(kOutbuf)]);
    }
}

/* Sections 34.2, 34.2.2 Figure 34-4: blocks outside the cropped S0 take S0BACKGROUND. */
void Imx28PxpFrame::FillPre(const uint32_t* regs, uint32_t pre_w, uint32_t pre_h) {
    const uint32_t  ctrl  = regs[Slot(kCtrl)];
    const uint32_t  block = (ctrl & kCtrlBlockSize) != 0u ? 16u : 8u;
    const BlockRect s0    = Fields(regs[Slot(kS0param)]);
    const BlockRect crop  = Fields(regs[Slot(kS0crop)]);
    /* Sections 34.4.9, 34.4.11: in 16x16 mode the field MSB must be 0. */
    if (block == 16u && ((regs[Slot(kS0param)] | regs[Slot(kS0crop)]) & 0x80808080u) != 0u)
        Die("16x16 block field MSB set", regs[Slot(kS0crop)]);
    pre_.assign(static_cast<size_t>(pre_w) * pre_h, To565(regs[Slot(kS0background)]));
    const uint32_t x0     = s0.xbase * block;
    const uint32_t y0     = s0.ybase * block;
    const uint32_t stride = s0.width * block * 2u;
    if (x0 >= pre_w || y0 >= pre_h) return;
    uint32_t w = crop.width * block;
    uint32_t h = crop.height * block;
    if (w > pre_w - x0) w = pre_w - x0;
    if (h > pre_h - y0) h = pre_h - y0;
    line_.resize(static_cast<size_t>(w) * 2u);
    auto& mem = emu_.Get<EmulatedMemory>();
    for (uint32_t r = 0; r < h; ++r) {
        const uint32_t pa = regs[Slot(kS0buf)] + (crop.ybase * block + r) * stride +
                            crop.xbase * block * 2u;
        if (mem.TryCopyOut(pa, line_.data(), w * 2u) != w * 2u) Die("S0 read outside RAM", pa);
        uint16_t* dst = pre_.data() + static_cast<size_t>(y0 + r) * pre_w + x0;
        for (uint32_t x = 0; x < w; ++x)
            dst[x] = static_cast<uint16_t>(line_[2u * x] | (line_[2u * x + 1u] << 8));
    }
}

/* Sections 34.2.3, 34.2.4, 34.4.9 to 34.4.16: S0 YUV420 (format 9, three planes) placed
   by S0PARAM, cropped and scaled by S0CROP/S0SCALE/S0OFFSET, converted by CSCCOEFF0-2. */
void Imx28PxpFrame::FillPreYuv420(const uint32_t* regs, uint32_t pre_w, uint32_t pre_h) {
    const uint32_t  ctrl  = regs[Slot(kCtrl)];
    const uint32_t  block = (ctrl & kCtrlBlockSize) != 0u ? 16u : 8u;
    const BlockRect s0    = Fields(regs[Slot(kS0param)]);
    const BlockRect crop  = Fields(regs[Slot(kS0crop)]);
    if (block == 16u && ((regs[Slot(kS0param)] | regs[Slot(kS0crop)]) & 0x80808080u) != 0u)
        Die("16x16 block field MSB set", regs[Slot(kS0crop)]);
    pre_.assign(static_cast<size_t>(pre_w) * pre_h, To565(regs[Slot(kS0background)]));
    const uint32_t src_w = s0.width * block;
    const uint32_t src_h = s0.height * block;
    const uint32_t x0    = s0.xbase * block;
    const uint32_t y0    = s0.ybase * block;
    if (src_w == 0u || src_h == 0u || x0 >= pre_w || y0 >= pre_h) return;
    const bool use_crop = (ctrl & kCtrlCrop) != 0u;
    const bool scale    = (ctrl & kCtrlScale) != 0u;
    uint32_t w = (use_crop ? crop.width : s0.width) * block;
    uint32_t h = (use_crop ? crop.height : s0.height) * block;
    if (w > pre_w - x0) w = pre_w - x0;
    if (h > pre_h - y0) h = pre_h - y0;
    const uint32_t org_x = use_crop ? crop.xbase * block : 0u;
    const uint32_t org_y = use_crop ? crop.ybase * block : 0u;
    /* Sections 34.4.12, 34.4.13: 3.12 step per output pixel; 0.12 offset in blocks. */
    const uint32_t s0scale  = regs[Slot(kS0scale)];
    const uint32_t s0offset = regs[Slot(kS0offset)];
    const uint32_t step_x = scale ? s0scale & 0x7FFFu : 0x1000u;
    const uint32_t step_y = scale ? (s0scale >> 16) & 0x7FFFu : 0x1000u;
    const uint32_t off_x  = scale ? (s0offset & 0xFFFu) * block : 0u;
    const uint32_t off_y  = scale ? ((s0offset >> 16) & 0xFFFu) * block : 0u;

    /* Section 34.4.12: a scale factor above 4 is not supported. */
    if (step_x == 0u || step_y == 0u || step_x > 0x4000u || step_y > 0x4000u)
        Die("S0SCALE outside 1/4096 to 4", s0scale);
    /* Section 34.4.7, 34.4.8: chroma pointers must be word aligned. */
    if (((regs[Slot(kS0ubuf)] | regs[Slot(kS0vbuf)]) & 0x3u) != 0u)
        Die("S0 chroma pointer not word aligned", regs[Slot(kS0ubuf)]);

    const uint32_t cw     = src_w / 2u;
    const uint32_t last_x = ((org_x << 12) + (w - 1u) * step_x + off_x) >> 12;
    const uint32_t last_y = ((org_y << 12) + (h - 1u) * step_y + off_y) >> 12;
    auto& mem = emu_.Get<EmulatedMemory>();
    const auto fetch = [&](std::vector<uint8_t>& dst, uint32_t pa, uint32_t stride, uint32_t x,
                           uint32_t y) {
        const uint32_t bytes = (y + 1u) * stride + x + 2u;
        dst.resize(bytes);
        if (mem.TryCopyOut(pa, dst.data(), bytes) != bytes) Die("S0 read outside RAM", pa);
    };
    fetch(plane_y_, regs[Slot(kS0buf)], src_w, last_x, last_y);
    fetch(plane_u_, regs[Slot(kS0ubuf)], cw, last_x >> 1, last_y >> 1);
    fetch(plane_v_, regs[Slot(kS0vbuf)], cw, last_x >> 1, last_y >> 1);

    const uint32_t k0  = regs[Slot(kCsccoeff0)];
    const uint32_t k1  = regs[Slot(kCsccoeff1)];
    const uint32_t k2  = regs[Slot(kCsccoeff2)];
    const int32_t  c0  = SignExtend(k0 >> 18, 11);
    const int32_t  uvo = SignExtend(k0 >> 9, 9);
    const int32_t  yo  = SignExtend(k0, 9);
    const int32_t  c1  = SignExtend(k1 >> 16, 11);
    const int32_t  c4  = SignExtend(k1, 11);
    const int32_t  c2  = SignExtend(k2 >> 16, 11);
    const int32_t  c3  = SignExtend(k2, 11);
    /* Section 34.4.14: YCBCR_MODE changes the U/V scaler in a way the manual leaves open. */
    if ((k0 & 0x80000000u) != 0u) Die("CSCCOEFF0 YCBCR_MODE", k0);

    for (uint32_t r = 0; r < h; ++r) {
        const uint32_t fy  = (org_y << 12) + r * step_y + off_y;
        uint16_t*      dst = pre_.data() + static_cast<size_t>(y0 + r) * pre_w + x0;
        for (uint32_t x = 0; x < w; ++x) {
            const uint32_t fx = (org_x << 12) + x * step_x + off_x;
            const int32_t  luma = static_cast<int32_t>(Bilinear(plane_y_, src_w, fx, fy));
            const int32_t  yl = c0 * (luma + yo);
            const int32_t  u  = static_cast<int32_t>(Bilinear(plane_u_, cw, fx >> 1, fy >> 1)) +
                               uvo;
            const int32_t  v  = static_cast<int32_t>(Bilinear(plane_v_, cw, fx >> 1, fy >> 1)) +
                               uvo;
            const uint32_t rgb = static_cast<uint32_t>(Saturate((yl + c1 * v) >> 8)) << 16 |
                                 static_cast<uint32_t>(Saturate((yl + c3 * u + c2 * v) >> 8)) << 8 |
                                 Saturate((yl + c4 * u) >> 8);
            dst[x] = To565(rgb);
        }
    }
}

/* Section 34.2.9 Figure 34-11: clockwise rotation of the whole frame. */
void Imx28PxpFrame::Rotate(uint32_t rotate, uint32_t out_w, uint32_t out_h, uint32_t pre_w,
                           uint32_t pre_h) {
    out_.resize(static_cast<size_t>(out_w) * out_h);
    for (uint32_t y = 0; y < out_h; ++y) {
        uint16_t* row = out_.data() + static_cast<size_t>(y) * out_w;
        for (uint32_t x = 0; x < out_w; ++x) {
            uint32_t px = x;
            uint32_t py = y;
            if (rotate == 1u)      { px = y;             py = pre_h - 1u - x; }
            else if (rotate == 2u) { px = pre_w - 1u - x; py = pre_h - 1u - y; }
            else if (rotate == 3u) { px = pre_w - 1u - y; py = x; }
            row[x] = pre_[static_cast<size_t>(py) * pre_w + px];
        }
    }
}

}
