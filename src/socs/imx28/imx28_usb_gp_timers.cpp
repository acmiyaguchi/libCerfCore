#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/state/state_stream.h>
#include "imx28_usb_gp_timers.h"
#include "imx28_usbctrl_regs.h"

#include <cstdint>

namespace cerfcore {

namespace {

/* Sections 31.3.7-31.3.10 GPTIMERnLD [23:0], GPTIMERnCTRL GPTRUN [31], GPTRST [30], GPTMODE
   [24], GPTCNT [23:0]. */
constexpr uint32_t kGptLdMask = 0x00FFFFFFu;
constexpr uint32_t kGptRun    = 1u << 31;
constexpr uint32_t kGptRst    = 1u << 30;
constexpr uint32_t kGptMode   = 1u << 24;

}  /* namespace */

void Imx28UsbGpTimers::Reset() {
    for (uint32_t i = 0; i < 2u; ++i) ld_[i] = ctrl_[i] = cnt_[i] = 0u;
}

bool Imx28UsbGpTimers::Read(uint32_t off, uint32_t& value) const {
    switch (off) {
        case kUsbGptLd0:   value = ld_[0]; return true;
        case kUsbGptLd1:   value = ld_[1]; return true;
        case kUsbGptCtrl0: value = ctrl_[0] | cnt_[0]; return true;
        case kUsbGptCtrl1: value = ctrl_[1] | cnt_[1]; return true;
        default:           return false;
    }
}

/* Section 31.3.8: GPTRUN starts a countdown that interrupts at zero; GPTRST loads GPTCNT. */
bool Imx28UsbGpTimers::Write(uint32_t off, uint32_t value) {
    uint32_t i = 0u;
    switch (off) {
        case kUsbGptLd0: ld_[0] = value & kGptLdMask; return true;
        case kUsbGptLd1: ld_[1] = value & kGptLdMask; return true;
        case kUsbGptCtrl0: i = 0u; break;
        case kUsbGptCtrl1: i = 1u; break;
        default: return false;
    }
    if ((value & kGptRun) != 0u) {
        emu_.Get<Fatal>().Die("Imx28 %08X: GPTIMER run at %08X (value %08X)", base_, base_ + off, value);
    }
    if ((value & kGptRst) != 0u) cnt_[i] = ld_[i];
    ctrl_[i] = value & kGptMode;
    return true;
}

void Imx28UsbGpTimers::SaveState(StateWriter& w) const {
    w.Write("gptld", ld_);
    w.Write("gptctrl", ctrl_);
    w.Write("gptcnt", cnt_);
}

void Imx28UsbGpTimers::RestoreState(StateReader& r) {
    r.Read("gptld", ld_);
    r.Read("gptctrl", ctrl_);
    r.Read("gptcnt", cnt_);
}

}
