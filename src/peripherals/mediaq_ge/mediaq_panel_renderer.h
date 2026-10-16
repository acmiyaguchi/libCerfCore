#pragma once

#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/panel_frame_renderer.h>
#include <cerfcore/lcd/lcd_pixel_expand.h>

#include <cstdint>
#include <cstring>

namespace cerfcore {

template <typename Chip>
class MediaQPanelRenderer : public PanelFrameRenderer {
public:
    using PanelFrameRenderer::PanelFrameRenderer;

    void OnReady() override { chip_ = &emu_.Get<Chip>(); }

    void PresentedSize(uint32_t& w, uint32_t& h) override {
        w = chip_->GetGuestW();
        h = chip_->GetGuestH();
    }

    bool HasFrame() override {
        Chip& mq = *chip_;
        if (!mq.IsEnabled())  return false;
        if (latch_.Latched()) return true;
        const uint32_t gh = mq.GetGuestH(), stride = mq.Stride();
        if (gh == 0 || stride == 0) return false;
        const uint64_t base  = mq.FbWindowOffset();
        const uint64_t bytes = static_cast<uint64_t>(stride) * gh;
        if (base + bytes > mq.FbSize()) return false;
        return latch_.ProbeAndLatch(mq.FbBytes() + base, static_cast<size_t>(bytes));
    }

    void RenderInto(uint32_t* dib, uint32_t host_w, uint32_t host_h) override {
        std::memset(dib, 0, static_cast<size_t>(host_w) * host_h * 4u);

        Chip& mq = *chip_;
        const uint32_t gw = mq.GetGuestW(), gh = mq.GetGuestH();
        const uint32_t bpp = mq.Bpp(), stride = mq.Stride();
        if (gw == 0 || gh == 0 || stride == 0 || bpp == 0) return;

        const uint8_t* fb   = mq.FbBytes();
        const uint32_t base = mq.FbWindowOffset();
        if (static_cast<uint64_t>(base) + static_cast<uint64_t>(stride) * gh > mq.FbSize())
            return;

        const uint32_t cw = (host_w < gw) ? host_w : gw;
        const uint32_t ch = (host_h < gh) ? host_h : gh;
        for (uint32_t y = 0; y < ch; ++y) {
            const uint8_t* line = fb + base + static_cast<size_t>(y) * stride;
            uint32_t* dst = dib + static_cast<size_t>(y) * host_w;
            for (uint32_t x = 0; x < cw; ++x)
                dst[x] = DecodePixel(line, x, bpp);
        }
    }

protected:
    /* MQ-200 Data Book Table 5-32; MediaQ doc 12-00026 Rev D Reg 4-58 (p.4-71):
       palette entry R[7:0] G[15:8] B[23:16]. */
    static uint32_t PalRgb(uint32_t e) {
        return lcd_pixel::PackXrgb(e & 0xFFu, (e >> 8) & 0xFFu, (e >> 16) & 0xFFu);
    }

    Chip* chip_ = nullptr;

private:
    uint32_t FromPalette(uint32_t index) const { return PalRgb(chip_->PaletteEntry(index)); }

    uint32_t DecodePixel(const uint8_t* line, uint32_t x, uint32_t bpp) const {
        if (bpp == 16u) return lcd_pixel::Expand565(cerf::le::U16(line, static_cast<size_t>(x) * 2u));
        if (bpp == 32u) return 0xFF000000u | (cerf::le::U32(line, static_cast<size_t>(x) * 4u) & 0xFFFFFFu);
        if (bpp == 24u) return 0xFF000000u | cerf::le::U24(line, static_cast<size_t>(x) * 3u);
        if (bpp == 8u) return FromPalette(line[x]);
        return FromPalette(lcd_pixel::PackedIndexMsbFirst(line, x, bpp));
    }
};

}
