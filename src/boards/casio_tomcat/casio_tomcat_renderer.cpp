#define NOMINMAX

#include "casio_tomcat_lcd.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/panel_frame_renderer.h>
#include "../../lcd/panel_scanout.h"
#include <cerfcore/boards/board_context.h>
#include "casio_tomcat_family_id.h"

namespace {

class CasioTomcatRenderer : public PanelFrameRenderer {
public:
    using PanelFrameRenderer::PanelFrameRenderer;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::CasioTomcat;
    }

    void PresentedSize(uint32_t& w, uint32_t& h) override {
        auto& lcd = emu_.Get<CasioTomcatLcd>();
        w = lcd.GuestW();
        h = lcd.GuestH();
    }

    bool HasFrame() override {
        auto& lcd = emu_.Get<CasioTomcatLcd>();
        lcd.MaybePublishDisplaySize();
        if (!lcd.IsDisplayEnabled()) return false;
        if (latch_.Latched())         return true;
        return latch_.ProbeAndLatch(lcd.FbBytes(), lcd.FbSize());
    }

    void RenderInto(uint32_t* dib, uint32_t host_w, uint32_t host_h) override {
        auto& lcd = emu_.Get<CasioTomcatLcd>();
        const PanelSurface src{.fb     = lcd.FbBytes(),
                               .stride = lcd.StrideBytes(),
                               .width  = lcd.GuestW(),
                               .height = lcd.GuestH()};
        scanout_.Blit(src, dib, host_w, host_h);
    }

private:
    /* casio_cassiopeia_e55 ddi.dll sub_14F1A94 stores sub_14F4DBC into a2[0]; sub_14F4DBC walks
       *(a2+12) pels along the octant at *(a2+28). Its 2bpp mask (2 << (bpp+31))-1 is 3 and its
       shift (idx*bpp) ^ (8-bpp) puts pixel 0 at bits 7:6; its op table runs in ROP2 order, so
       case 1's 0 is black and case 16's ~0 is white. */
    PanelScanout scanout_{PanelPixelFormat::kGray2MsbInverted};
};

}  /* namespace */

REGISTER_SERVICE_AS(CasioTomcatRenderer, PanelFrameRenderer);
