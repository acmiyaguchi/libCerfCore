#define NOMINMAX

#include "../../socs/sa11xx/sa11xx_lcd.h"

#include <cerfcore/boards/board_context.h>
#include "compaq_ipaq_h3000_family_id.h"
#include "compaq_ipaq_h3000_panel.h"
#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/cpu/emulated_memory.h>
#include <cerfcore/host/panel_frame_renderer.h>
#include <cerfcore/lcd/lcd_pixel_expand.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {

/* SA-1110 §11.7.1.2: in active TFT 16-bpp mode (PBS=2, PAS=1), the
   frame buffer begins with a 32-byte dummy palette buffer that the
   LCD controller loads but does not use, followed by 16-bit pixel
   data laid out per Figure 11-9: R[15:11] G[10:5] B[4:0]. */

constexpr uint32_t kDummyPaletteBytes  = 32;
constexpr uint32_t kBytesPerGuestPixel = 2;

/* Panel is 320x240: Linux arch/arm/mach-sa1100/h3600.c h3600_lcd_info and
   h3100.c h3100_lcd_info, both .xres = 320, .yres = 240. */
constexpr uint32_t kPanelLines = 240;

/* SA-1110 §11.7.1.5 Table 11-7: dither value -> pixel-ON duty. The H31xx
   STN glass darkens when driven (Linux h3100_lcd_info: cmap_inverse=1;
   the ROM's ddi.dll programs an identity hw palette relying on it), so
   intensity = duty complement - an ascending table inverts the screen. */
constexpr uint8_t kMonoGray[16] = { 255, 227, 204, 187, 170, 153, 142, 127,
                                    113, 102, 85, 68, 51, 28, 0, 0 };

/* PBS, palette entry 0 bits 13:12 (devman p.11-19): 00=4bpp/32-byte
   palette, 01=8bpp/512-byte palette. The manual misprints the 4bpp row
   as "0x", which would overlap the "01 - 8 bits" row; 01 is 8 bpp. */
struct MonoLayout {
    uint32_t data_off;
    uint32_t bits_per_px;
    uint32_t pal_entries;
};

class CompaqIpaqH3000LcdRenderer : public PanelFrameRenderer {
public:
    using PanelFrameRenderer::PanelFrameRenderer;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::CompaqIpaqH3000;
    }

    void PresentedSize(uint32_t& w, uint32_t& h) override {
        w = PanelH();
        h = emu_.Get<Sa11xxLcd>().GetGuestW();
    }

    bool HasFrame() override {
        auto& lcd = emu_.Get<Sa11xxLcd>();
        if (!lcd.IsEnabled())              return false;
        if (latch_.Latched())              return true;
        const uint32_t fb_pa   = lcd.GetFbPa();
        const uint32_t guest_w = lcd.GetGuestW();
        const uint32_t guest_h = PanelH();
        if (fb_pa == 0 || guest_w == 0 || guest_h == 0) return false;

        uint32_t data_off    = kDummyPaletteBytes;
        size_t   pixel_bytes = (size_t)guest_w * (size_t)guest_h
                             * (size_t)kBytesPerGuestPixel;
        if (!lcd.IsColor()) {
            const uint8_t* fb_base =
                emu_.Get<EmulatedMemory>().TryTranslate(fb_pa);
            if (!fb_base) return false;
            const MonoLayout ml = ResolveMonoLayout(fb_base);
            data_off    = ml.data_off;
            pixel_bytes = (size_t)guest_w * (size_t)guest_h
                        * (size_t)ml.bits_per_px / 8u;
        }
        return latch_.ProbeAndLatch(emu_.Get<EmulatedMemory>(),
                                    fb_pa + data_off,
                                    pixel_bytes);
    }

    void RenderInto(uint32_t* dib_bgra32,
                    uint32_t  host_w,
                    uint32_t  host_h) override {
        auto& lcd = emu_.Get<Sa11xxLcd>();
        const uint32_t fb_pa   = lcd.GetFbPa();
        const uint32_t guest_w = lcd.GetGuestW();
        const uint32_t guest_h = PanelH();

        std::memset(dib_bgra32, 0, (size_t)host_w * host_h * 4u);
        if (guest_w == 0 || guest_h == 0) return;

        const uint8_t* fb_base = emu_.Get<EmulatedMemory>().TryTranslate(fb_pa);
        if (!fb_base) return;

        const CompaqIpaqH3000Panel::Kind panel =
            emu_.Get<CompaqIpaqH3000Panel>().PanelKind();
        if (lcd.IsColor()) {
            RenderColorInto(dib_bgra32, host_w, host_h,
                            fb_base, guest_w, guest_h, panel);
        } else {
            RenderMonoInto(dib_bgra32, host_w, host_h,
                           fb_base, guest_w, guest_h, panel.rotate_cw);
        }
    }

private:
    uint32_t PanelH() {
        return std::min(emu_.Get<Sa11xxLcd>().GetGuestH(), kPanelLines);
    }

    MonoLayout ResolveMonoLayout(const uint8_t* fb_base) {
        const uint16_t entry0 = cerf::le::U16(fb_base);
        const uint32_t pbs = (entry0 >> 12) & 0x3u;
        switch (pbs) {
            case 0: return MonoLayout{ 0x20u,  4u, 16u  };
            case 1: return MonoLayout{ 0x200u, 8u, 256u };
            default: break;
        }
        /* PBS=10 bypasses the palette with 12-bit RGB pixels (p.11-19) -
           contradicts CMS=1's 4-bit gray palette path; PBS=11 reserved.
           Neither has defined mono behavior. */
        LOG(Lcd, "CompaqIpaqH3000LcdRenderer: FATAL mono frame with PBS=%u "
                 "(palette entry0=0x%04X) - undefined combination\n",
            pbs, entry0);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }

    template <typename Fetch>
    static void RotateInto(uint32_t* dib_bgra32, uint32_t host_w, uint32_t host_h,
                           uint32_t guest_w, uint32_t guest_h, bool rotate_cw,
                           Fetch fetch) {
        const uint32_t copy_w = std::min(host_w, guest_h);
        const uint32_t copy_h = std::min(host_h, guest_w);
        for (uint32_t y_dst = 0; y_dst < copy_h; ++y_dst) {
            const uint32_t x_src = rotate_cw ? y_dst : guest_w - 1u - y_dst;
            uint32_t* dst_row = dib_bgra32 + (size_t)y_dst * host_w;
            for (uint32_t x_dst = 0; x_dst < copy_w; ++x_dst) {
                const uint32_t y_src = rotate_cw ? guest_h - 1u - x_dst : x_dst;
                dst_row[x_dst] = fetch((size_t)y_src * guest_w + x_src);
            }
        }
    }

    static uint32_t PanelGray(uint32_t xrgb) {
        const uint8_t g = kMonoGray[15u - (lcd_pixel::Luma8(xrgb) >> 4)];
        return lcd_pixel::PackXrgb(g, g, g);
    }

    void RenderColorInto(uint32_t* dib_bgra32,
                         uint32_t host_w, uint32_t host_h,
                         const uint8_t* fb_base,
                         uint32_t guest_w, uint32_t guest_h,
                         CompaqIpaqH3000Panel::Kind panel) {
        const uint8_t* pixels = fb_base + kDummyPaletteBytes;
        auto rgb = [pixels](size_t idx) {
            return lcd_pixel::Expand565(cerf::le::U16(pixels, idx * kBytesPerGuestPixel));
        };
        if (panel.color) {
            RotateInto(dib_bgra32, host_w, host_h, guest_w, guest_h,
                       panel.rotate_cw, rgb);
        } else {
            RotateInto(dib_bgra32, host_w, host_h, guest_w, guest_h,
                       panel.rotate_cw,
                       [&rgb](size_t idx) { return PanelGray(rgb(idx)); });
        }
    }

    void RenderMonoInto(uint32_t* dib_bgra32,
                        uint32_t host_w, uint32_t host_h,
                        const uint8_t* fb_base,
                        uint32_t guest_w, uint32_t guest_h,
                        bool rotate_cw) {
        const MonoLayout ml = ResolveMonoLayout(fb_base);

        /* §11.7.1: the palette is re-transferred from the frame buffer
           every frame; mirror that by rebuilding the LUT per render. */
        uint32_t lut[256];
        for (uint32_t i = 0; i < ml.pal_entries; ++i) {
            const uint16_t e = cerf::le::U16(fb_base, i * 2);
            const uint8_t  g = kMonoGray[e & 0xFu];
            lut[i] = lcd_pixel::PackXrgb(g, g, g);
        }
        if (!mono_palette_logged_) {
            mono_palette_logged_ = true;
            char pal[16 * 5 + 1];
            for (uint32_t i = 0; i < 16; ++i)
                std::snprintf(pal + i * 5, 6, "%04X ", cerf::le::U16(fb_base, i * 2));
            LOG(Lcd, "CompaqIpaqH3000LcdRenderer: mono frame %ubpp, palette %s\n",
                ml.bits_per_px, pal);
        }

        const uint8_t* data   = fb_base + ml.data_off;
        const bool     packed = ml.bits_per_px == 4u;
        RotateInto(dib_bgra32, host_w, host_h, guest_w, guest_h, rotate_cw,
                   [data, packed, &lut](size_t idx) {
            if (!packed) return lut[data[idx]];
            /* Fig 11-4 (little endian): pixel 0 in bits 3:0,
               pixel 1 in bits 7:4. */
            const uint8_t b = data[idx >> 1];
            return lut[(idx & 1u) ? (b >> 4) : (b & 0xFu)];
        });
    }

    bool            mono_palette_logged_ = false;
};

}  /* namespace */

REGISTER_SERVICE_AS(CompaqIpaqH3000LcdRenderer, PanelFrameRenderer);
