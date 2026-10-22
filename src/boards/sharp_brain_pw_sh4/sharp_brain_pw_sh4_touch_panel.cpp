#include "sharp_brain_pw_sh4_touch_panel.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include "sharp_brain_pw_sh4_family_id.h"

#include <cstdint>

namespace cerfcore {

namespace {

/* MCIMX28RM Rev. 2 section 38.5.1 CTRL0: TOUCH_DETECT_ENABLE [23], TOUCH_SCREEN_TYPE [22],
   YNLRSW [21], YPLLSW [20:19], XNURSW [18:17], XPULSW [16]. */
constexpr uint32_t kDetectEnable = 1u << 23;
constexpr uint32_t kFiveWire     = 1u << 22;
constexpr uint32_t kSwitchMask   = 0x003F0000u;

/* Table 38-2 4-wire X-axis and Y-axis columns; sharp_brain_pw_sh4 lradc.dll c06723c0 pressure
   drive XNURSW 2, YPLLSW 1. */
constexpr uint32_t kXAxis    = 0x00050000u;
constexpr uint32_t kYAxis    = 0x00280000u;
constexpr uint32_t kPressure = 0x000C0000u;

/* Figure 38-2: LRADC2 xpul, LRADC3 ypll, LRADC4 xnur, LRADC5 ynlr; drive switches to 1.85 V. */
constexpr uint32_t kXpul    = 2u;
constexpr uint32_t kYpll    = 3u;
constexpr uint32_t kXnur    = 4u;
constexpr uint32_t kYnlr    = 5u;
constexpr uint32_t kDriveMv = 1850u;
constexpr double   kCodes   = 4096.0;

/* sharp_brain_pw_sh4 touchraw.dll TRD_InitHw c06c1ae8 plate value 600; TRD_SampleXYR c06c1948
   accepts 1 <= R < 0x800 (c06c1ec0). */
constexpr double kPlateOhms   = 600.0;
constexpr double kContactOhms = 1024.0;

uint32_t CodeMillivolts(uint32_t code) {
    return static_cast<uint32_t>((static_cast<uint64_t>(code) * kDriveMv + 2048u) / 4096u);
}

uint32_t RoundMv(double mv) { return static_cast<uint32_t>(mv + 0.5); }

}  /* namespace */

bool SharpBrainPwSh4TouchPanel::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::SharpBrainPwSh4;
}

void SharpBrainPwSh4TouchPanel::SetContact(bool down, uint32_t raw_x, uint32_t raw_y) {
    down_  = down;
    raw_x_ = raw_x;
    raw_y_ = raw_y;
    if (on_change_) on_change_();
}

bool SharpBrainPwSh4TouchPanel::OwnsPin(uint32_t pin) const {
    return pin >= kXpul && pin <= kYnlr;
}

/* sharp_brain_pw_sh4 touchraw.dll TRD_WaitReadChannel c06c17c8 grounds both plates after each
   read. */
uint32_t SharpBrainPwSh4TouchPanel::PinMillivolts(uint32_t pin, uint32_t ctrl0) {
    const uint32_t sw = ctrl0 & kSwitchMask;
    if ((ctrl0 & kFiveWire) == 0u && sw == kXAxis) {
        if (pin == kXpul) return kDriveMv;
        if (pin == kXnur) return 0u;
        return down_ ? CodeMillivolts(raw_x_) : 0u;
    }
    if ((ctrl0 & kFiveWire) == 0u && sw == kYAxis) {
        if (pin == kYpll) return kDriveMv;
        if (pin == kYnlr) return 0u;
        return down_ ? CodeMillivolts(raw_y_) : 0u;
    }
    if ((ctrl0 & kFiveWire) == 0u && sw == kPressure) {
        if (pin == kYpll) return kDriveMv;
        if (pin == kXnur) return 0u;
        if (!down_) return pin == kXpul ? 0u : kDriveMv;
        const double rx = kPlateOhms * raw_x_ / kCodes;
        const double ry = kPlateOhms * (kCodes - raw_y_) / kCodes;
        const double ma = kDriveMv / (rx + ry + kContactOhms);
        return pin == kXpul ? RoundMv(ma * rx) : RoundMv(ma * (rx + kContactOhms));
    }
    emu_.Get<Fatal>().Die("SharpBrainPwSh4TouchPanel: pin %u conversion with CTRL0 %08X", pin,
                          ctrl0);
}

/* Table 38-2 4-wire touch detect column. */
bool SharpBrainPwSh4TouchPanel::Detected(uint32_t ctrl0) {
    if ((ctrl0 & kDetectEnable) == 0u) return false;
    if ((ctrl0 & (kFiveWire | kSwitchMask)) != 0u) {
        emu_.Get<Fatal>().Die("SharpBrainPwSh4TouchPanel: touch detect with CTRL0 %08X", ctrl0);
    }
    return down_;
}

REGISTER_SERVICE(SharpBrainPwSh4TouchPanel);

}
