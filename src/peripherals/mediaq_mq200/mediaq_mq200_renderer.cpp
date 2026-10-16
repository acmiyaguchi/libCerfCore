#define NOMINMAX

#include "mediaq_mq200.h"

#include "mediaq_mq200_config.h"
#include "../mediaq_ge/mediaq_panel_renderer.h"
#include <cerfcore/core/byte_order.h>

#include <algorithm>

namespace cerfcore {

namespace {

class MediaQMq200Renderer : public MediaQPanelRenderer<MediaQMq200> {
public:
    using MediaQPanelRenderer<MediaQMq200>::MediaQPanelRenderer;

    bool ShouldRegister() override {
        return emu_.TryGet<MediaQMq200Config>() != nullptr;
    }

    bool HasFrame() override {
        MediaQMq200& mq = *chip_;
        if (!mq.IsEnabled()) return false;
        if (latch_.Latched()) return true;
        const MediaQMq200Gc::Window win = mq.PanelImage();
        const int64_t row   = (static_cast<int64_t>(win.w) * MediaQMq200Gc::DepthBits(win.depth) + 7) / 8;
        const int64_t span  = static_cast<int64_t>(win.h - 1u) * win.stride;
        const int64_t first = static_cast<int64_t>(win.start) + std::min<int64_t>(span, 0);
        const int64_t last  = static_cast<int64_t>(win.start) + std::max<int64_t>(span, 0) + row;
        if (first < 0 || last > static_cast<int64_t>(mq.FbSize())) return false;
        return latch_.ProbeAndLatch(mq.FbBytes() + first, static_cast<size_t>(last - first));
    }

    void RenderInto(uint32_t* dib, uint32_t host_w, uint32_t host_h) override {
        std::fill(dib, dib + static_cast<size_t>(host_w) * host_h, 0u);
        MediaQMq200& mq = *chip_;
        if (!mq.IsEnabled()) return;
        const MediaQMq200Gc::Window win = mq.PanelImage();
        const uint32_t dw = std::min(host_w, mq.GetGuestW());
        const uint32_t dh = std::min(host_h, mq.GetGuestH());
        for (uint32_t row = 0; row < win.h && win.y + row < dh; ++row) {
            const int64_t line = static_cast<int64_t>(win.start) + static_cast<int64_t>(row) * win.stride;
            uint32_t*     dst  = dib + static_cast<size_t>(win.y + row) * host_w;
            for (uint32_t col = 0; col < win.w && win.x + col < dw; ++col) {
                dst[win.x + col] = WindowPixel(win, line, col);
            }
        }
        const MediaQMq200::HwCursor c = mq.PanelCursor();
        if (c.shown) DrawCursor(c, dib, host_w, host_h);
    }

private:
    static constexpr uint32_t kCursorSide     = 64u;
    static constexpr uint32_t kCursorRowBytes = 16u;
    static constexpr uint32_t kFbAddrMask     = 0x001FFFFFu;

    uint8_t FbByte(int64_t addr) const {
        return chip_->FbBytes()[static_cast<uint32_t>(addr) & kFbAddrMask];
    }

    uint32_t Lut(uint32_t r, uint32_t g, uint32_t b) const {
        return lcd_pixel::PackXrgb(chip_->PaletteEntry(r) & 0xFFu,
                                   (chip_->PaletteEntry(g) >> 8) & 0xFFu,
                                   (chip_->PaletteEntry(b) >> 16) & 0xFFu);
    }

    uint32_t Direct(uint32_t r, uint32_t g, uint32_t b, uint32_t depth) const {
        if (MediaQMq200Gc::DepthUsesPalette(depth)) return Lut(r, g, b);
        return lcd_pixel::PackXrgb(r, g, b);
    }

    /* MQ-200 Data Book pp. 3-15 / 3-16 (frame buffer formats, LUT expansion), Table 5-8,
       Table 5-21 (AW1PI), C100R-C1FFR (palette entry R[7:0] G[15:8] B[23:16]). */
    uint32_t WindowPixel(const MediaQMq200Gc::Window& win, int64_t line, uint32_t col) const {
        const uint32_t bits = MediaQMq200Gc::DepthBits(win.depth);
        if (MediaQMq200Gc::DepthIndexed(win.depth)) {
            const uint32_t bit   = col * bits;
            const uint8_t  byte  = FbByte(line + bit / 8u);
            const uint32_t index = (byte >> lcd_pixel::MsbFirstShift(bit, bits)) & ((1u << bits) - 1u);
            return PalRgb(chip_->PaletteEntry(win.palette_base | index));
        }
        const int64_t at = line + static_cast<int64_t>(col) * (bits / 8u);
        if (bits == 16u) {
            const uint32_t px = FbByte(at) | (static_cast<uint32_t>(FbByte(at + 1)) << 8);
            return Direct(lcd_pixel::Expand5((px >> 11) & 0x1Fu), lcd_pixel::Expand6((px >> 5) & 0x3Fu),
                          lcd_pixel::Expand5(px & 0x1Fu), win.depth);
        }
        const uint32_t lo = FbByte(at), mid = FbByte(at + 1), hi = FbByte(at + 2);
        if (bits == 24u || (win.depth & 1u) == 0u) return Direct(hi, mid, lo, win.depth);
        return Direct(lo, mid, hi, win.depth);
    }

    /* MQ-200 Data Book Table 3-16: AND/XOR 00 background, 01 foreground, 10 transparent,
       11 inverse; X0 / A0 sit in bits 16 / 0 of each 16-pixel word. */
    void DrawCursor(const MediaQMq200::HwCursor& c, uint32_t* dib, uint32_t host_w,
                    uint32_t host_h) const {
        MediaQMq200&   mq  = *chip_;
        const uint32_t w   = std::min(host_w, mq.GetGuestW());
        const uint32_t h   = std::min(host_h, mq.GetGuestH());
        const uint8_t* img = mq.FbBytes() + c.image;
        for (uint32_t cy = c.y_off; cy < kCursorSide; ++cy) {
            const uint32_t sy = c.y + (cy - c.y_off);
            if (sy >= h) break;
            for (uint32_t cx = c.x_off; cx < kCursorSide; ++cx) {
                const uint32_t sx = c.x + (cx - c.x_off);
                if (sx >= w) break;
                const uint32_t word = cerf::le::U32(img, cy * kCursorRowBytes + (cx / 16u) * 4u);
                const uint32_t and_bit = (word >> (cx % 16u)) & 1u;
                const uint32_t xor_bit = (word >> (16u + cx % 16u)) & 1u;
                uint32_t& px = dib[static_cast<size_t>(sy) * host_w + sx];
                if (and_bit && !xor_bit) continue;
                if (and_bit) {
                    px ^= 0x00FFFFFFu;
                    continue;
                }
                px = PalRgb(xor_bit ? c.fg : c.bg);
            }
        }
    }
};

}

REGISTER_SERVICE_AS(MediaQMq200Renderer, PanelFrameRenderer);

}
