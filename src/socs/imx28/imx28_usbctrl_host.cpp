#include <cerfcore/core/cerf_emulator.h>
#include "../../execution/guest_cycle_clock.h"
#include "imx28_usbctrl.h"

#include <cstdint>

namespace cerfcore {

namespace {

/* MCIMX28RM Rev. 2 § 31.3.20 FRINDEX [13:0], one micro-frame every 125 microseconds. */
constexpr uint32_t kFrIndexBits       = 0x00003FFFu;
constexpr int64_t  kNsPerMicroFrame   = 125000;

}  /* namespace */

/* § 31.3.17 USBCMD: RS runs the host schedules, ASE and PSE enable the asynchronous and
   periodic schedule walks, IAA rings the async advance doorbell; § 31.3.32 the port starts
   with no device connected. */
void Imx28Usbctrl::WriteHostCmd(uint32_t value, uint32_t addr) {
    if ((value & (kUsbCmdAse | kUsbCmdPse | kUsbCmdIaa)) != 0u) {
        Die("host schedule walk (ASE, PSE or IAA)", addr, value);
    }
    if ((r_[kCmd] & kUsbCmdRs) == 0u && (value & kUsbCmdRs) != 0u) {
        run_ns_ = emu_.Get<GuestCycleClock>().NowNs();
    }
    if ((r_[kCmd] & kUsbCmdRs) != 0u && (value & kUsbCmdRs) == 0u) {
        r_[kFrIndex] = ComposeFrIndex();
    }
}

/* § 31.3.20: the host controller advances FRINDEX every micro-frame while RS is set; in
   device mode the SOF marker updates it. */
uint32_t Imx28Usbctrl::ComposeFrIndex() const {
    if (Mode() != kUsbModeHost || (r_[kCmd] & kUsbCmdRs) == 0u) return r_[kFrIndex];
    const int64_t elapsed = emu_.Get<GuestCycleClock>().NowNs() - run_ns_;
    return static_cast<uint32_t>(r_[kFrIndex] + elapsed / kNsPerMicroFrame) & kFrIndexBits;
}

/* § 31.3.20: "This register cannot be written unless the Host Controller is in the
   'Halted' state"; read-only in device mode. */
void Imx28Usbctrl::WriteFrIndex(uint32_t value, uint32_t addr) {
    if (Mode() != kUsbModeHost) return;
    if ((r_[kCmd] & kUsbCmdRs) != 0u) Die("FRINDEX write while running", addr, value);
    r_[kFrIndex] = value & kFrIndexBits;
}

}
