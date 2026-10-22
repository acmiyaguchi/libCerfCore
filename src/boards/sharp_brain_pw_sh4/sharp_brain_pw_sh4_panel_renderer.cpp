#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/host/panel_frame_renderer.h>
#include <cerfcore/lcd/lcd_pixel_expand.h>
#include "../../socs/mpu_lcd_panel.h"
#include "sharp_brain_pw_sh4_family_id.h"

#include <cstdint>
#include <cstring>

namespace cerfcore {

namespace {

/* sharp_brain_pw_sh4 ddraw_mx28.dll UpdateScreenRegion_PXP c06d5534, c06d5b40: GWES pixel
   (x, y) goes to frame memory column 479 - y, page x; UpdateExtraScreen c06d848c pages 320h
   to 355h. */
constexpr uint32_t kHostWidth  = 854u;
constexpr uint32_t kHostHeight = 480u;

class SharpBrainPwSh4PanelRenderer : public PanelFrameRenderer {
public:
    using PanelFrameRenderer::PanelFrameRenderer;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SharpBrainPwSh4;
    }

    void PresentedSize(uint32_t& w, uint32_t& h) override {
        w = kHostWidth;
        h = kHostHeight;
    }

    bool HasFrame() override {
        if (latch_.Latched()) return true;
        const PanelSurface g = emu_.Get<MpuLcdPanel>().Gram();
        return latch_.ProbeAndLatch(g.fb, static_cast<size_t>(g.stride) * g.height);
    }

    void RenderInto(uint32_t* dib, uint32_t host_w, uint32_t host_h) override {
        std::memset(dib, 0, static_cast<size_t>(host_w) * host_h * 4u);
        const PanelSurface g = emu_.Get<MpuLcdPanel>().Gram();
        const uint32_t w = host_w < kHostWidth ? host_w : kHostWidth;
        const uint32_t h = host_h < kHostHeight ? host_h : kHostHeight;
        if (g.width != kHostHeight || g.height != kHostWidth) {
            emu_.Get<Fatal>().Die("Panel surface %ux%u, expected %ux%u", g.width, g.height,
                                  kHostHeight, kHostWidth);
        }
        for (uint32_t y = 0; y < h; ++y) {
            uint32_t* row = dib + static_cast<size_t>(y) * host_w;
            const uint32_t column = kHostHeight - 1u - y;
            for (uint32_t x = 0; x < w; ++x) {
                const size_t at = static_cast<size_t>(x) * g.stride + column * 2u;
                row[x] = lcd_pixel::Expand565(cerf::le::U16(g.fb, at));
            }
        }
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(SharpBrainPwSh4PanelRenderer, PanelFrameRenderer);

}
