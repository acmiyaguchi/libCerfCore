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

}  /* namespace */

void Imx28PxpFrame::Die(const char* what, uint32_t value) const {
    emu_.Get<Fatal>().Die("Imx28 PXP: %s (value %08X)", what, value);
}

void Imx28PxpFrame::Render(const uint32_t* regs) {
    const uint32_t ctrl = regs[Slot(kCtrl)];
    const uint32_t s0_fmt = (ctrl & kCtrlS0Format) >> 12;
    const uint32_t out_fmt = (ctrl & kCtrlOutFormat) >> 4;
    if ((ctrl & kCtrlUnmodelled) != 0u || s0_fmt != kFormatRgb565 || out_fmt != kFormatRgb565) {
        // Tolerated unmodelled mode (e.g. video preview / scaling / YUV format)
        return;
    }
    if ((ctrl & kCtrlCrop) == 0u) Die("CTRL CROP clear", ctrl);
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
    FillPre(regs, out_w, out_h);
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
