#include "imx28_rtc_watchdog.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/guest_deep_sleep.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include "imx28_clkctrl.h"

namespace cerfcore {

namespace {

/* Section 22.8.6 WATCHDOG: reset FFFF_FFFFh. */
constexpr uint32_t kWatchdogReset = 0xFFFFFFFFu;

/* Section 22.8.13 DEBUG: WATCHDOG_RESET_MASK [1], WATCHDOG_RESET [0]. */
constexpr uint32_t kDebugResetMask = 1u << 1;
constexpr uint32_t kDebugReset     = 1u << 0;

}  /* namespace */

void Imx28RtcWatchdog::Init(std::function<void()> on_event) {
    clock_ = &emu_.Get<GuestCycleClock>();
    event_ = clock_->Add(std::move(on_event));
}

void Imx28RtcWatchdog::Reset() {
    value_      = kWatchdogReset;
    anchor_     = ms_.Now();
    reset_mask_ = false;
    clock_->Disarm(event_);
}

void Imx28RtcWatchdog::Disarm() { clock_->Disarm(event_); }

void Imx28RtcWatchdog::Arm(bool run) {
    if (run && !reset_mask_) ms_.ArmAt(event_, anchor_ + value_);
    else                     clock_->Disarm(event_);
}

uint32_t Imx28RtcWatchdog::Value(bool enabled) {
    if (!enabled) return value_;
    const uint64_t elapsed = ms_.Now() - anchor_;
    return elapsed >= value_ ? 0u : value_ - static_cast<uint32_t>(elapsed);
}

void Imx28RtcWatchdog::Rebase(bool enabled) {
    value_  = Value(enabled);
    anchor_ = ms_.Now();
}

void Imx28RtcWatchdog::Write(Imx28SctOp op, uint32_t value, bool enabled) {
    value_  = Imx28ApplySct(op, Value(enabled), value);
    anchor_ = ms_.Now();
}

void Imx28RtcWatchdog::WriteDebug(Imx28SctOp op, uint32_t value) {
    const uint32_t old = reset_mask_ ? kDebugResetMask : 0u;
    reset_mask_ = (Imx28ApplySct(op, old, value) & kDebugResetMask) != 0u;
}

uint32_t Imx28RtcWatchdog::DebugBits(bool enabled) {
    return (reset_mask_ ? kDebugResetMask : 0u) |
           ((enabled && Value(true) == 0u) ? kDebugReset : 0u);
}

/* MCIMX28RM Rev. 2 section 22.8.13 WATCHDOG_RESET_MASK; section 10.8.27 WDOG_POR_DISABLE: 0
   "watchdog_reset will trigger a POR", 1 "same with software reset HW_CLKCTRL_RESET.CHIP";
   section 11.3.4: "The watchdog timer expires while enabled" starts the power-down sequence. */
void Imx28RtcWatchdog::OnExpiry() {
    if (reset_mask_) return;
    if (emu_.Get<Imx28Clkctrl>().WatchdogPorDisabled()) {
        emu_.Get<GuestCpuReset>().WatchdogReset();
        return;
    }
    emu_.Get<GuestDeepSleep>().Enter();
}

void Imx28RtcWatchdog::Save(StateWriter& w) {
    w.Write("wd_value", value_);
    w.Write("wd_anchor", anchor_);
    w.Write<uint8_t>("wd_reset_mask", reset_mask_ ? 1u : 0u);
}

void Imx28RtcWatchdog::Restore(StateReader& r) {
    uint8_t mask = 0;
    r.Read("wd_value", value_);
    r.Read("wd_anchor", anchor_);
    r.Read("wd_reset_mask", mask);
    reset_mask_ = mask != 0u;
    clock_->Disarm(event_);
}

}
