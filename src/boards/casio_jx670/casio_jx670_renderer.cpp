#define NOMINMAX

#include "casio_jx670_companion.h"

#include <cerfcore/boards/board_context.h>
#include "casio_jx670_family_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/panel_frame_renderer.h>
#include "../../lcd/panel_scanout.h"

namespace cerfcore {

namespace {

/* ddi.dll @0xFC5458-0xFC546C (andi 0xF800/0x7E0/0x1F channel split): the 16bpp
   framebuffer is RGB565. */
class CasioJx670Renderer : public PanelFrameRenderer {
public:
    using PanelFrameRenderer::PanelFrameRenderer;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::CasioJx670;
    }

    void PresentedSize(uint32_t& w, uint32_t& h) override {
        auto& asic = emu_.Get<CasioJx670Companion>();
        w = asic.GuestW();
        h = asic.GuestH();
    }

    bool HasFrame() override {
        auto& asic = emu_.Get<CasioJx670Companion>();
        if (!asic.IsDisplayEnabled()) return false;
        if (latch_.Latched())         return true;
        const uint32_t bytes = asic.StrideBytes() * asic.GuestH();
        return latch_.ProbeAndLatch(asic.FbBytes(), bytes);
    }

    void RenderInto(uint32_t* dib, uint32_t host_w, uint32_t host_h) override {
        auto& asic = emu_.Get<CasioJx670Companion>();
        const PanelSurface src{.fb     = asic.FbBytes(),
                               .stride = asic.StrideBytes(),
                               .width  = asic.GuestW(),
                               .height = asic.GuestH()};
        scanout_.Blit(src, dib, host_w, host_h);
    }

private:
    PanelScanout scanout_{PanelPixelFormat::kRgb565Le};
};

}  /* namespace */

REGISTER_SERVICE_AS(CasioJx670Renderer, PanelFrameRenderer);

}
