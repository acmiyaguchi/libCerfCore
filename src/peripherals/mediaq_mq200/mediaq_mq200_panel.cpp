#include "mediaq_mq200_panel.h"

#include "mediaq_mq200_fp.h"
#include "mediaq_mq200_gc.h"
#include "mediaq_mq200_power.h"

namespace {

uint32_t ModuleBit(uint32_t gc, uint32_t gc1_bit, uint32_t gc2_bit) {
    return gc == MediaQMq200Gc::kGc1 ? gc1_bit : gc2_bit;
}

}

bool MediaQMq200Panel::Shown(bool fb_served) const {
    const uint32_t gc = fp_.PanelGc();
    if (gc == MediaQMq200Fp::kNoPanel || !fb_served || !fp_.PanelPinsHigh()) return false;
    if (!power_.Powered(MediaQMq200Power::kModFp) ||
        !power_.Powered(ModuleBit(gc, MediaQMq200Power::kModGc1, MediaQMq200Power::kModGc2)) ||
        !power_.Powered(ModuleBit(gc, MediaQMq200Power::kModWin1, MediaQMq200Power::kModWin2))) {
        return false;
    }
    return gc_.Displays(gc) && Width() > 1u && Height() > 1u;
}

MediaQMq200Gc::Window MediaQMq200Panel::Image() const {
    const uint32_t gc = fp_.PanelGc();
    if (gc == MediaQMq200Fp::kNoPanel) return MediaQMq200Gc::Window{};
    return gc_.ActiveWindow(gc, power_);
}

uint32_t MediaQMq200Panel::Bpp() const {
    if (fp_.PanelGc() == MediaQMq200Fp::kNoPanel) return 0u;
    return MediaQMq200Gc::DepthBits(Image().depth);
}

uint32_t MediaQMq200Panel::Width() const {
    const uint32_t gc = fp_.PanelGc();
    if (gc == MediaQMq200Fp::kNoPanel) return 0u;
    return gc_.DisplayWidth(gc);
}

uint32_t MediaQMq200Panel::Height() const {
    const uint32_t gc = fp_.PanelGc();
    if (gc == MediaQMq200Fp::kNoPanel) return 0u;
    return gc_.DisplayHeight(gc);
}

uint32_t MediaQMq200Panel::WindowOffset() const {
    return Image().start;
}

uint32_t MediaQMq200Panel::Stride() const {
    return static_cast<uint16_t>(Image().stride);
}

uint32_t MediaQMq200Panel::Palette(uint32_t index) const {
    return gc_.Palette(index);
}

MediaQMq200Panel::HwCursor MediaQMq200Panel::Cursor(bool fb_served) const {
    HwCursor c;
    if (!Shown(fb_served)) return c;
    const uint32_t gc = fp_.PanelGc();
    if (!(gc_.Reg(gc, MediaQMq200Gc::kCtrl) & MediaQMq200Gc::kCtrlCursorEnable) ||
        !power_.Powered(
            ModuleBit(gc, MediaQMq200Power::kModCursor1, MediaQMq200Power::kModCursor2))) {
        return c;
    }
    const uint32_t pos  = gc_.Reg(gc, MediaQMq200Gc::kCursorPos);
    const uint32_t addr = gc_.Reg(gc, MediaQMq200Gc::kCursorAddr);
    c.shown = true;
    c.x     = pos & 0xFFFu;
    c.y     = (pos >> 16) & 0xFFFu;
    c.image = (addr & 0x7FFu) << 10;
    c.x_off = (addr >> 16) & 0x3Fu;
    c.y_off = (addr >> 24) & 0x3Fu;
    c.fg    = gc_.Reg(gc, MediaQMq200Gc::kCursorFg);
    c.bg    = gc_.Reg(gc, MediaQMq200Gc::kCursorBg);
    return c;
}
