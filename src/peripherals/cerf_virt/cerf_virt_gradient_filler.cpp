#include "cerf_virt_gradient_filler.h"
#include "cerf_virt_blt_pixelops.h"
#include <cerfcore/peripherals/cerf_virt/cerf_virt_framebuffer.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/log.h>

#include <vector>

namespace cerfcore {

namespace CerfVirt {

REGISTER_SERVICE(CerfVirtGradientFiller);

bool CerfVirtGradientFiller::ShouldRegister() {
    return emu_.Get<DeviceConfig>().guest_additions;
}

void CerfVirtGradientFiller::OnReady() {
    fb_  = &emu_.Get<CerfVirtFramebuffer>();
    arena_  = &emu_.Get<CerfVirtDmaArena>();
}

namespace {

int64_t GradFloorDiv(int64_t num, int64_t span) {
    int64_t q = num / span;
    if (num % span != 0 && num < 0) --q;
    return q;
}
}

bool CerfVirtGradientFiller::Execute(const CerfGradDescriptor& g) {
    if (g.magic != kCerfGradMagic) {
        LOG(Cerf, "[CerfVirtGradientFiller] corrupt gradient magic 0x%08X\n", g.magic);
        CerfFatalExit();
    }
    const int32_t span = g.end_coord - g.start_coord;
    if (span <= 0) return true;

    const uint32_t d_bits = BltPixelOps::FormatBits(g.dst.format);
    if (d_bits <= 8u) {
        LOG(Cerf, "[CerfVirtGradientFiller] UNIMPLEMENTED indexed/sub-byte dst "
                  "format %u (%u bpp)\n", g.dst.format, d_bits);
        CerfFatalExit();
    }
    uint32_t d_masks[3], d_bpp = 0, d_shift[3];
    if (!ResolveMasks(g.dst, d_masks, &d_bpp)) {
        LOG(Cerf, "[CerfVirtGradientFiller] UNIMPLEMENTED dst format %u (%u bpp)\n",
            g.dst.format, d_bits);
        CerfFatalExit();
    }
    for (int i = 0; i < 3; ++i) d_shift[i] = 32u - BltPixelOps::HighBitPos(d_masks[i]);

    const uint32_t a_mask  = (g.dst.pal_entries == 4u) ? g.dst.mask[3] : 0u;
    const uint32_t a_shift = a_mask ? 32u - BltPixelOps::HighBitPos(a_mask) : 0u;

    Surface dst;
    if (!ResolveSurface(g.dst, d_bpp, &dst)) {
        LOG(Cerf, "[CerfVirtGradientFiller] dst surface unresolvable: buffer=0x%08X "
                  "is_fb_pa=%u fmt=%u stride=%d stage_off=0x%08X stage_len=%u\n",
            g.dst.buffer, g.dst.is_fb_pa, g.dst.format, g.dst.stride,
            g.dst.stage_off, g.dst.stage_len);
        CerfFatalExit();
    }

    int64_t base[4], step[4];
    for (int c = 0; c < 4; ++c) {
        base[c] = (int64_t)((uint32_t)g.start_color[c] << 8) << 32;
        const int64_t delta = ((int64_t)g.end_color[c] << 8) - ((int64_t)g.start_color[c] << 8);
        step[c] = GradFloorDiv(delta << 32, span);
    }

    const CerfBltRect& r = g.fill_rect;
    const bool horiz = (g.axis == kCerfGradAxisH);
    const int32_t n = horiz ? (r.right - r.left) : (r.bottom - r.top);
    if (n <= 0) return true;
    const int32_t origin = horiz ? r.left : r.top;
    std::vector<uint32_t> line((size_t)n);
    for (int32_t i = 0; i < n; ++i) {
        int32_t t = (origin + i) - g.start_coord;
        if (t < 0) t = 0; else if (t > span) t = span;
        uint32_t px = 0u;
        for (int c = 0; c < 3; ++c) {
            const uint8_t c8 = (uint8_t)((base[c] + step[c] * (int64_t)t) >> 48);
            px |= (((uint32_t)c8 << 24) >> d_shift[c]) & d_masks[c];
        }
        if (a_mask) {
            const uint8_t a8 = (uint8_t)((base[3] + step[3] * (int64_t)t) >> 48);
            px |= (((uint32_t)a8 << 24) >> a_shift) & a_mask;
        }
        line[(size_t)i] = px;
    }

    for (int32_t y = r.top; y < r.bottom; ++y) {
        if (g.band_y_count != 0u &&
            ((uint32_t)y < g.band_y_first ||
             (uint32_t)y >= g.band_y_first + g.band_y_count)) continue;
        const uint32_t row_px = horiz ? 0u : line[(size_t)(y - r.top)];
        for (int32_t x = r.left; x < r.right; ++x) {
            const uint32_t px = horiz ? line[(size_t)(x - r.left)] : row_px;
            uint32_t run = 0;
            uint8_t* dp = PixelPtr(dst, x, y, d_bpp, &run);
            if (!dp) {
                LOG(Cerf, "[CerfVirtGradientFiller] dst pixel unaddressable at x=%d y=%d: "
                          "buffer=0x%08X is_fb_pa=%u stage_off=0x%08X stage_len=%u\n",
                    x, y, g.dst.buffer, g.dst.is_fb_pa, g.dst.stage_off, g.dst.stage_len);
                CerfFatalExit();
            }
            if (run >= d_bpp) BltPixelOps::WritePixel(dp, d_bpp, px);
            else              WriteStraddlePixel(dst, x, y, d_bpp, px);
        }
    }
    fb_->MarkDirty();
    return true;
}

}

}
