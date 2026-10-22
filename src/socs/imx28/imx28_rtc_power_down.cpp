#include "imx28_rtc_power_down.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/host/guest_deep_sleep.h>
#include <cerfcore/state/state_stream.h>
#include "imx28_rtc_persistent0.h"

#include <algorithm>

namespace cerfcore {

namespace {

/* Section 22.8.7 AUTO_RESTART: "automatically power up approximately 180 ms after powering
   down". */
constexpr int64_t kAutoRestartDelayNs = 180000000;

}  /* namespace */

/* Sections 11.3.4.1 (the RTC runs while the chip is powered down), 22.8.7 XTAL24MHZ_PWRUP,
   ALARM_EN, ALARM_WAKE_EN and 22.6 (an alarm event powers up a powered-down chip). */
void Imx28RtcPowerDown::Enter(uint32_t p0, uint64_t next_alarm_tick) {
    if ((p0 & kP0Clocksource) == 0u && (p0 & kP0Xtal24Pwrup) == 0u) {
        emu_.Get<Fatal>().Die("Imx28Rtc: power-down with PERSISTENT0 %08X (CLOCKSOURCE 0, "
                              "XTAL24MHZ_PWRUP 0)", p0);
    }
    down_             = true;
    auto_restart_     = (p0 & kP0AutoRestart) != 0u;
    auto_restart_due_ = sleep_->SleptNs() + kAutoRestartDelayNs;
    alarm_            = (p0 & kP0AlarmEn) != 0u && (p0 & kP0AlarmWakeEn) != 0u;
    alarm_tick_       = next_alarm_tick;
}

/* Section 22.8.7 ALARM_WAKE: "Set when the chip is powered up by an alarm event from rtc_ana". */
uint32_t Imx28RtcPowerDown::Exit() {
    if (!down_) return 0u;
    const bool alarm_woke = alarm_ && sec_.Now() >= alarm_tick_;
    down_         = false;
    auto_restart_ = false;
    alarm_        = false;
    return alarm_woke ? kP0AlarmWake : 0u;
}

bool Imx28RtcPowerDown::SourceDue() {
    if (!down_) return false;
    return (auto_restart_ && sleep_->SleptNs() >= auto_restart_due_) ||
           (alarm_ && sec_.Now() >= alarm_tick_);
}

int64_t Imx28RtcPowerDown::DueNs() {
    int64_t due = GuestDeepSleep::kNoParkWake;
    if (!down_) return due;
    if (auto_restart_) due = auto_restart_due_;
    if (alarm_) due = std::min(due, sec_.SleptNsAtTick(alarm_tick_));
    return due;
}

void Imx28RtcPowerDown::Save(StateWriter& w) {
    w.Write<uint8_t>("pd_down", down_ ? 1u : 0u);
    w.Write<uint8_t>("pd_alarm", alarm_ ? 1u : 0u);
    w.Write("pd_alarm_tick", alarm_tick_);
    w.Write<uint8_t>("pd_auto_restart", auto_restart_ ? 1u : 0u);
    w.Write("pd_auto_restart_due", auto_restart_due_);
}

void Imx28RtcPowerDown::Restore(StateReader& r) {
    uint8_t down = 0, alarm = 0, auto_restart = 0;
    r.Read("pd_down", down);
    r.Read("pd_alarm", alarm);
    r.Read("pd_alarm_tick", alarm_tick_);
    r.Read("pd_auto_restart", auto_restart);
    r.Read("pd_auto_restart_due", auto_restart_due_);
    down_         = down != 0u;
    alarm_        = alarm != 0u;
    auto_restart_ = auto_restart != 0u;
}

}
