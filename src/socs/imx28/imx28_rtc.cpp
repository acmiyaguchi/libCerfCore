#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/host/guest_deep_sleep.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include "../../execution/guest_cycle_clock.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../oscillator_ticks.h"
#include "imx28_apb_peripheral.h"
#include "imx28_icoll.h"
#include "imx28_id.h"
#include "imx28_rtc_crystal_fuses.h"
#include "imx28_rtc_persistent0.h"
#include "imx28_rtc_persistent1_5.h"
#include "imx28_rtc_power_down.h"
#include "imx28_rtc_seconds_seed.h"
#include "imx28_rtc_watchdog.h"

#include <cstdint>

namespace cerfcore {

namespace {

/* Section 22.8 memory map: one register per 0x10 bytes, 0x8005_6000 to 0x8005_60D0. */
constexpr uint32_t kRegCount        = 0x0Eu;
constexpr uint32_t kRegCtrl         = 0x0u;
constexpr uint32_t kRegStat         = 0x1u;
constexpr uint32_t kRegMilliseconds = 0x2u;
constexpr uint32_t kRegSeconds      = 0x3u;
constexpr uint32_t kRegAlarm        = 0x4u;
constexpr uint32_t kRegWatchdog     = 0x5u;
constexpr uint32_t kRegPersistent0  = 0x6u;
constexpr uint32_t kRegPersistent5  = 0xBu;
constexpr uint32_t kRegDebug        = 0xCu;
constexpr uint32_t kRegVersion      = 0xDu;

/* Section 22.8.1 CTRL: reset C000_0020h; SUPPRESS_COPY2ANALOG [6], FORCE_UPDATE [5],
   WATCHDOGEN [4], ONEMSEC_IRQ [3], ALARM_IRQ [2], ONEMSEC_IRQ_EN [1], ALARM_IRQ_EN [0]. */
constexpr uint32_t kCtrlReset        = 0xC0000020u;
constexpr uint32_t kCtrlSuppress     = 1u << 6;
constexpr uint32_t kCtrlForceUpdate  = 1u << 5;
constexpr uint32_t kCtrlWatchdogEn   = 1u << 4;
constexpr uint32_t kCtrlOnemsecIrq   = 1u << 3;
constexpr uint32_t kCtrlAlarmIrq     = 1u << 2;
constexpr uint32_t kCtrlOnemsecIrqEn = 1u << 1;
constexpr uint32_t kCtrlAlarmIrqEn   = 1u << 0;
constexpr uint32_t kCtrlWritable     = 0xC000007Fu;

/* Section 22.8.2 STAT: RTC_PRESENT [31], ALARM_PRESENT [30], WATCHDOG_PRESENT [29],
   XTAL32000_PRESENT [28], XTAL32768_PRESENT [27], STALE_REGS [23:16], NEW_REGS [15:8]. */
constexpr uint32_t kStatPresent   = 0xE0000000u;
constexpr uint32_t kStatXtal32768 = 1u << 27;
constexpr uint32_t kStatXtal32000 = 1u << 28;
constexpr uint32_t kStatStaleAll  = 0xFFu;
constexpr uint32_t kStatStaleShift = 16u;

/* Section 22.1: the copy controller orders registers Persistent 0-5, Alarm, Seconds as STALE_REGS
   bits 0-7. */
constexpr uint32_t kStaleAlarm   = 1u << 6;
constexpr uint32_t kStaleSeconds = 1u << 7;

/* Section 22.8.14 VERSION: reset 0203_0000h. */
constexpr uint32_t kVersion = 0x02030000u;

/* Sections 22.2 and 22.5: the second counter's 1 Hz time base and the 1 kHz millisecond
   pulse. */
constexpr uint64_t kSecondsHz   = 1u;
constexpr uint64_t kMsHz        = 1000u;
constexpr uint64_t kSecondsWrap = 0x100000000ull;

/* Table 5-1 sources 28 (rtc_1msec_irq) and 29 (rtc_alarm_irq). */
constexpr uint32_t kIrqOneMsec = 28u;
constexpr uint32_t kIrqAlarm   = 29u;

/* MCIMX28RM Rev. 2 chapter 22 (RTC, alarm, watchdog, persistent bits) at 0x8005_6000. */
class Imx28Rtc : public Imx28ApbPeripheral {
public:
    using Imx28ApbPeripheral::Imx28ApbPeripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return 0x80056000u; }
    uint32_t MmioSize() const override { return 0x00002000u; }

    uint32_t ReadWord (uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override;

protected:
    uint32_t PeekLanes(uint32_t word_addr, uint32_t lanes) override;

private:
    static constexpr uint32_t kPersistRegs = 6u;

    bool     Active() const { return !Gated(ctrl_); }
    bool     WatchdogEnabled() const { return (ctrl_ & kCtrlWatchdogEn) != 0u; }
    uint32_t SecondsNow();
    uint32_t MillisecondsNow();
    uint64_t NextAlarmTick(uint64_t from) const;
    uint32_t ShadowPersistent(uint32_t i) const;
    void     RequireMsRes(const char* use) const;
    void     Settle();
    void     Rearm();
    void     DriveLines();
    void     DigitalReset();
    void     ApplyGate(uint32_t old_ctrl);
    void     RunCopyController();
    void     OnTimerEvent();
    void     OnWatchdogEvent();
    void     CheckAlarmEqual();
    void     WriteCtrl(Imx28SctOp op, uint32_t value);
    void     WriteShadow(uint32_t index, Imx28SctOp op, uint32_t value);

    StoppableOscillatorTicks ms_{emu_, false};
    OscillatorTicks          sec_{emu_, true};
    Imx28RtcWatchdog         wd_{emu_, ms_};
    Imx28RtcPowerDown        pd_{emu_, sec_};

    GuestCycleClock*        clock_       = nullptr;
    Imx28Icoll*             icoll_       = nullptr;
    GuestCycleClock::Event* alarm_event_ = nullptr;
    GuestCycleClock::Event* ms_event_    = nullptr;

    uint32_t master_persist_[kPersistRegs] = {};
    uint32_t master_alarm_  = 0u;
    uint32_t sec_base_      = 0u;
    uint64_t alarm_eval_    = 0u;

    uint32_t ctrl_          = 0u;
    uint32_t stale_         = 0u;
    uint32_t ms_base_       = 0u;
    uint64_t ms_stat_base_  = 0u;
    bool     xtal32768_     = false;
    bool     xtal32000_     = false;
    bool     alarm_line_    = false;
    bool     ms_line_       = false;
};

}  /* namespace */

bool Imx28Rtc::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Imx28;
}

void Imx28Rtc::OnReady() {
    clock_ = &emu_.Get<GuestCycleClock>();
    icoll_ = &emu_.Get<Imx28Icoll>();
    sec_.Attach(kSecondsHz, 1u);
    ms_.Attach(kMsHz, 1u);
    alarm_event_ = clock_->Add([this] { OnTimerEvent(); });
    ms_event_    = clock_->Add([this] { OnTimerEvent(); });
    wd_.Init([this] { OnWatchdogEvent(); });
    master_persist_[0] = kPersist0Reset;
    sec_base_          = emu_.Get<Imx28RtcSecondsSeed>().SecondsAtHostNow();
    xtal32768_         = emu_.Get<Imx28RtcCrystalFuses>().Xtal32768Present();
    xtal32000_         = emu_.Get<Imx28RtcCrystalFuses>().Xtal32000Present();
    DigitalReset();
    clock_->RegisterRateListener([this] {
        sec_.Rescale();
        ms_.Rescale();
        Rearm();
    });
    auto& sleep = emu_.Get<GuestDeepSleep>();
    pd_.Init(sleep);
    sleep.RegisterSleepEntryListener(
        [this] { pd_.Enter(master_persist_[0], NextAlarmTick(sec_.Now())); });
    sleep.RegisterParkClock([this] { OnTimerEvent(); });
    sleep.RegisterParkWakeSource([this] { return pd_.SourceDue(); });
    sleep.RegisterParkWakeDue([this] { return pd_.DueNs(); });
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
        DigitalReset();
        DriveLines();
    });
    emu_.Get<GuestCpuReset>().RegisterResetReleaseListener(
        [this] { master_persist_[0] |= pd_.Exit(); });
    emu_.Get<PeripheralDispatcher>().Register(this);
    LOG(SocRtc, "Imx28Rtc: SECONDS seeded %u\n", sec_base_);
}

/* Section 22.4: "The normal digital reset has no effect on the master RTC registers". */
void Imx28Rtc::DigitalReset() {
    ms_.SetCounting(false);
    ms_base_       = 0u - static_cast<uint32_t>(ms_.Now());
    ms_stat_base_  = ms_.Now();
    alarm_eval_    = sec_.Now();
    wd_.Reset();
    ctrl_          = kCtrlReset;
    stale_         = kStatStaleAll;
    clock_->Disarm(alarm_event_);
    clock_->Disarm(ms_event_);
}

uint32_t Imx28Rtc::SecondsNow() { return sec_base_ + static_cast<uint32_t>(sec_.Now()); }

uint32_t Imx28Rtc::MillisecondsNow() {
    RequireMsRes("MILLISECONDS");
    return ms_base_ + static_cast<uint32_t>(ms_.Now());
}

/* Section 22.8.7 MSEC_RES: "Resolutions supported are: 1, 2, 4, 8, and 16 msec". */
void Imx28Rtc::RequireMsRes(const char* use) const {
    const uint32_t res = (master_persist_[0] >> kP0MsecResShift) & kP0MsecResMask;
    if (res != kMsecRes1ms) {
        emu_.Get<Fatal>().Die("Imx28Rtc: %s used with PERSISTENT0 MSEC_RES %02X", use, res);
    }
}

/* Sections 22.8.4 and 22.8.5: the digital reset value "is only visible until the copy
   controller overwrites" it. */
uint32_t Imx28Rtc::ShadowPersistent(uint32_t i) const {
    if ((stale_ & (1u << i)) == 0u) return master_persist_[i];
    return i == 0u ? kPersist0Reset : 0u;
}

/* Section 22.6: "continuously matched" against the seconds counter; an alarm event needs
   PERSISTENT0.ALARM_EN (22.8.7) and is latched in CTRL.ALARM_IRQ. */
uint64_t Imx28Rtc::NextAlarmTick(uint64_t from) const {
    const uint32_t away = master_alarm_ - (sec_base_ + static_cast<uint32_t>(from));
    return from + (away != 0u ? away : kSecondsWrap);
}

void Imx28Rtc::CheckAlarmEqual() {
    if (Active() && (master_persist_[0] & kP0AlarmEn) != 0u && SecondsNow() == master_alarm_) {
        ctrl_ |= kCtrlAlarmIrq;
    }
}

/* Section 22.8.1 ONEMSEC_IRQ: "one millisecond interrupt request status". */
void Imx28Rtc::Settle() {
    const uint64_t s_now = sec_.Now();
    if (Active()) {
        const uint64_t m_now = ms_.Now();
        if ((master_persist_[0] & kP0AlarmEn) != 0u && NextAlarmTick(alarm_eval_) <= s_now) {
            ctrl_ |= kCtrlAlarmIrq;
        }
        if (m_now > ms_stat_base_) {
            RequireMsRes("ONEMSEC_IRQ");
            ctrl_ |= kCtrlOnemsecIrq;
        }
        ms_stat_base_ = m_now;
    }
    alarm_eval_ = s_now;
}

void Imx28Rtc::Rearm() {
    if (Active() && (master_persist_[0] & kP0AlarmEn) != 0u && (ctrl_ & kCtrlAlarmIrqEn) != 0u &&
        (ctrl_ & kCtrlAlarmIrq) == 0u) {
        sec_.ArmAt(alarm_event_, NextAlarmTick(alarm_eval_));
    } else {
        clock_->Disarm(alarm_event_);
    }
    if (Active() && (ctrl_ & kCtrlOnemsecIrqEn) != 0u && (ctrl_ & kCtrlOnemsecIrq) == 0u) {
        RequireMsRes("ONEMSEC_IRQ_EN");
        ms_.ArmAt(ms_event_, ms_stat_base_ + 1u);
    } else {
        clock_->Disarm(ms_event_);
    }
    const bool wd_run = Active() && WatchdogEnabled();
    if (wd_run) RequireMsRes("WATCHDOGEN");
    wd_.Arm(wd_run);
}

void Imx28Rtc::DriveLines() {
    const bool alarm = (ctrl_ & kCtrlAlarmIrq) != 0u && (ctrl_ & kCtrlAlarmIrqEn) != 0u;
    const bool ms    = (ctrl_ & kCtrlOnemsecIrq) != 0u && (ctrl_ & kCtrlOnemsecIrqEn) != 0u;
    if (alarm != alarm_line_) {
        alarm_line_ = alarm;
        if (alarm) icoll_->AssertSource(kIrqAlarm);
        else       icoll_->DeassertSource(kIrqAlarm);
    }
    if (ms != ms_line_) {
        ms_line_ = ms;
        if (ms) icoll_->AssertSource(kIrqOneMsec);
        else    icoll_->DeassertSource(kIrqOneMsec);
    }
}

void Imx28Rtc::OnTimerEvent() {
    Settle();
    Rearm();
    DriveLines();
}

void Imx28Rtc::OnWatchdogEvent() {
    Settle();
    if (wd_.Value(WatchdogEnabled()) != 0u) {
        Rearm();
        return;
    }
    wd_.OnExpiry();
}

/* Section 22.3 and Figure 22-2: "This releases the copy controller" once SFTRST and CLKGATE
   are cleared; FORCE_UPDATE (22.8.1) reloads all eight shadows and hardware clears it. */
void Imx28Rtc::RunCopyController() {
    if (!Active()) return;
    if (stale_ == 0u && (ctrl_ & kCtrlForceUpdate) == 0u) return;
    stale_ = 0u;
    ctrl_ &= ~kCtrlForceUpdate;
}

/* Section 22.8.1 SFTRST: "Hold real time clock digital side in soft reset state"; CLKGATE:
   "gates off the clocks to the block". */
void Imx28Rtc::ApplyGate(uint32_t old_ctrl) {
    const bool was = (old_ctrl & (kCtrlSftrst | kCtrlClkgate)) == 0u;
    if (was == Active()) return;
    ms_.SetCounting(Active());
    ms_stat_base_ = ms_.Now();
    alarm_eval_   = sec_.Now();
}

void Imx28Rtc::WriteCtrl(Imx28SctOp op, uint32_t value) {
    Settle();
    const uint32_t old  = ctrl_;
    uint32_t       next = Imx28ApplySct(op, old, value);
    next = (old & ~kCtrlWritable) | (next & kCtrlWritable);
    if ((next & kCtrlSuppress) != 0u) {
        emu_.Get<Fatal>().Die("Imx28Rtc: CTRL %08X sets SUPPRESS_COPY2ANALOG", next);
    }
    if (IsSoftReset(old, next)) {
        DigitalReset();
        DriveLines();
        return;
    }
    if (((old ^ next) & kCtrlWatchdogEn) != 0u) wd_.Rebase(WatchdogEnabled());
    ctrl_ = next;
    ApplyGate(old);
    RunCopyController();
    Rearm();
    DriveLines();
}

void Imx28Rtc::WriteShadow(uint32_t index, Imx28SctOp op, uint32_t value) {
    Settle();
    if (index >= kRegPersistent0) {
        const uint32_t i   = index - kRegPersistent0;
        const uint32_t old = master_persist_[i];
        uint32_t       next = Imx28ApplySct(op, old, value);
        if (i == 0u && (old & kP0LckSecs) != 0u) next |= kP0LckSecs;
        if (i == 0u) {
            if (const char* why = Imx28RtcP0Reject(old, next, xtal32768_, xtal32000_)) {
                emu_.Get<Fatal>().Die("Imx28Rtc: PERSISTENT0 %08X -> %08X (changed %08X): %s", old,
                                      next, old ^ next, why);
            }
        }
        if (i != 0u) {
            if (const char* why = Imx28RtcPersistReject(i, old, next)) {
                emu_.Get<Fatal>().Die("Imx28Rtc: PERSISTENT%u %08X -> %08X (changed %08X): %s", i,
                                      old, next, old ^ next, why);
            }
        }
        master_persist_[i] = next;
        if (i == 0u && (old & kP0AlarmEn) == 0u) CheckAlarmEqual();
    } else if (index == kRegAlarm) {
        master_alarm_ = Imx28ApplySct(op, master_alarm_, value);
        CheckAlarmEqual();
    } else {
        if ((master_persist_[0] & kP0LckSecs) != 0u) return;
        sec_base_ = Imx28ApplySct(op, SecondsNow(), value) - static_cast<uint32_t>(sec_.Now());
        CheckAlarmEqual();
    }
    Rearm();
    DriveLines();
}

uint32_t Imx28Rtc::ReadWord(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    if (off >= kRegCount * 0x10u || (off & 0x3u) != 0u) HaltUnsupportedAccess("ReadWord", addr, 0);
    const Imx28SctAccess a = Imx28DecodeSct(off);
    const uint32_t index = a.reg_offset >> 4;
    if (a.op != Imx28SctOp::Write) {
        if (index == kRegVersion || index == kRegStat) HaltUnsupportedAccess("ReadWord", addr, 0);
        return 0u;
    }
    switch (index) {
        case kRegCtrl:         Settle(); return ctrl_;
        case kRegStat:
            return kStatPresent | (xtal32768_ ? kStatXtal32768 : 0u) |
                   (xtal32000_ ? kStatXtal32000 : 0u) | (stale_ << kStatStaleShift);
        case kRegMilliseconds: return MillisecondsNow();
        case kRegSeconds:      return (stale_ & kStaleSeconds) != 0u ? 0u : SecondsNow();
        case kRegAlarm:        return (stale_ & kStaleAlarm) != 0u ? 0u : master_alarm_;
        case kRegWatchdog:     return wd_.Value(WatchdogEnabled());
        case kRegVersion:      return kVersion;
        default:               break;
    }
    if (index >= kRegPersistent0 && index <= kRegPersistent5) {
        return ShadowPersistent(index - kRegPersistent0);
    }
    HaltUnsupportedAccess("ReadWord", addr, 0);
}

void Imx28Rtc::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - MmioBase();
    if (off >= kRegCount * 0x10u || (off & 0x3u) != 0u) HaltUnsupportedAccess("WriteWord", addr, value);
    const Imx28SctAccess a = Imx28DecodeSct(off);
    const uint32_t index = a.reg_offset >> 4;
    if (index == kRegVersion || index == kRegStat) HaltUnsupportedAccess("WriteWord", addr, value);
    if (index == kRegCtrl) {
        WriteCtrl(a.op, value);
        return;
    }
    RequireOperating("WriteWord", ctrl_, addr, value);
    switch (index) {
        case kRegMilliseconds:
            ms_base_ = Imx28ApplySct(a.op, MillisecondsNow(), value) - static_cast<uint32_t>(ms_.Now());
            return;
        case kRegWatchdog:
            wd_.Write(a.op, value, WatchdogEnabled());
            Rearm();
            return;
        case kRegDebug:
            wd_.WriteDebug(a.op, value);
            Rearm();
            return;
        default:           break;
    }
    if (index == kRegSeconds || index == kRegAlarm ||
        (index >= kRegPersistent0 && index <= kRegPersistent5)) {
        WriteShadow(index, a.op, value);
        return;
    }
    HaltUnsupportedAccess("WriteWord", addr, value);
}

uint32_t Imx28Rtc::PeekLanes(uint32_t word_addr, uint32_t lanes) {
    if (word_addr - MmioBase() == (kRegDebug << 4)) return wd_.DebugBits(WatchdogEnabled());
    return ReadLanes(word_addr, lanes);
}

void Imx28Rtc::SaveState(StateWriter& w) {
    sec_.Save(w, "sec_");
    ms_.Save(w, "ms_");
    w.WriteBytes("master_persist", master_persist_, sizeof(master_persist_));
    w.Write("master_alarm", master_alarm_);
    w.Write("sec_base", sec_base_);
    w.Write("alarm_eval", alarm_eval_);
    w.Write("ctrl", ctrl_);
    w.Write("stale", stale_);
    w.Write("ms_base", ms_base_);
    w.Write("ms_stat_base", ms_stat_base_);
    wd_.Save(w);
    w.Write<uint8_t>("alarm_line", alarm_line_ ? 1u : 0u);
    w.Write<uint8_t>("ms_line", ms_line_ ? 1u : 0u);
    pd_.Save(w);
}

void Imx28Rtc::RestoreState(StateReader& r) {
    sec_.Restore(r, "sec_");
    ms_.Restore(r, "ms_");
    r.ReadBytes("master_persist", master_persist_, sizeof(master_persist_));
    r.Read("master_alarm", master_alarm_);
    r.Read("sec_base", sec_base_);
    r.Read("alarm_eval", alarm_eval_);
    r.Read("ctrl", ctrl_);
    r.Read("stale", stale_);
    r.Read("ms_base", ms_base_);
    r.Read("ms_stat_base", ms_stat_base_);
    wd_.Restore(r);
    uint8_t alarm = 0, ms = 0;
    r.Read("alarm_line", alarm);
    r.Read("ms_line", ms);
    alarm_line_    = alarm != 0u;
    ms_line_       = ms != 0u;
    pd_.Restore(r);
    clock_->Disarm(alarm_event_);
    clock_->Disarm(ms_event_);
}

void Imx28Rtc::PostRestore() {
    Rearm();
    DriveLines();
}

REGISTER_SERVICE(Imx28Rtc);

}
