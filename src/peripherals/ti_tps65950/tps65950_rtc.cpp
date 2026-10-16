#include "tps65950_rtc.h"

#include <cerfcore/boards/board_context.h>
#include "../../boards/ti_omap35x_evm/ti_omap35x_evm_family_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/host/guest_deep_sleep.h>
#include <cerfcore/state/state_stream.h>
#include "tps65950_interrupts.h"
#include "tps65950_reset_image.h"

#include <algorithm>

namespace cerfcore {

namespace {

/* TPS65950 TRM SWCU050E page 122: a compensated second lasts (32,768 - COMP_REG)/32,768 seconds of the 32-kHz
   counter; Table 3-64 (page 146): the 7FFF value is forbidden. */
constexpr uint64_t kTicksPerSecond = 32768u;
constexpr int64_t  kCompForbidden  = 0x7FFF;

/* TPS65950 TRM SWCU050E Table 3-58 (page 144, page 145): RTC_CTRL_REG. */
constexpr uint8_t kStopRtc      = 1u << 0;
constexpr uint8_t kRound30s     = 1u << 1;
constexpr uint8_t kAutoComp     = 1u << 2;
constexpr uint8_t kMode1224     = 1u << 3;
constexpr uint8_t kTestMode     = 1u << 4;
constexpr uint8_t kSet32Counter = 1u << 5;
constexpr uint8_t kGetTime      = 1u << 6;
constexpr uint8_t kCtrlStored   = kStopRtc | kAutoComp | kMode1224;

/* TPS65950 TRM SWCU050E Table 3-60 (page 145): RTC_STATUS_REG. */
constexpr uint8_t kPowerUp = 1u << 7;
constexpr uint8_t kAlarm   = 1u << 6;
constexpr uint8_t k1dEvent = 1u << 5;
constexpr uint8_t k1hEvent = 1u << 4;
constexpr uint8_t k1mEvent = 1u << 3;
constexpr uint8_t k1sEvent = 1u << 2;
constexpr uint8_t kRun     = 1u << 1;
constexpr uint8_t kEvents  = k1dEvent | k1hEvent | k1mEvent | k1sEvent;

/* TPS65950 TRM SWCU050E Table 3-62 (page 146): RTC_INTERRUPTS_REG. */
constexpr uint8_t kItAlarm = 1u << 3;
constexpr uint8_t kItTimer = 1u << 2;
constexpr uint8_t kEvery   = 0x03u;

/* TPS65950 TRM SWCU050E Tables 3-44 (page 141) and 3-46 to 3-56 (pages 141-144): WEEKS_REG [7:3] reserved RW; the
   alarm registers' reserved bits are R. */
constexpr uint8_t kWeeksReserved = 0xF8u;
constexpr uint8_t kAlarmMask[Tps65950RtcCalendar::kAlarmCount] = {0x7Fu, 0x7Fu, 0xBFu, 0x3Fu, 0x1Fu, 0xFFu};

constexpr uint64_t kNever = ~0ull;

/* TPS65950 TRM SWCU050E page 122: ROUND_30S sets the time to the closest minute during the next second. */
uint8_t StepBoundary(Tps65950RtcCalendar& cal, bool round) { return round ? cal.RoundToMinute() : cal.StepSecond(); }

}

bool Tps65950Rtc::ShouldRegister() {
    return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::TiOmap35xEvm;
}

void Tps65950Rtc::OnReady() {
    clock_      = &emu_.Get<GuestCycleClock>();
    interrupts_ = &emu_.Get<Tps65950Interrupts>();
    event_      = clock_->Add([this] { OnEvent(); });
    osc_.Attach(kTicksPerSecond, 1u);
    LoadPowerOn();
    osc_.SetCounting(Running());
    clock_->RegisterRateListener([this] {
        osc_.Rescale();
        Arm();
    });
    emu_.Get<GuestDeepSleep>().RegisterParkClock([this] { OnEvent(); });
}

void Tps65950Rtc::LoadPowerOn() {
    for (uint8_t i = 0; i < Cal::kRegCount; ++i) {
        const uint8_t value = tps65950::PowerRtcPowerOn(static_cast<uint8_t>(kSeconds + i));
        if (!cal_.Write(static_cast<Cal::Reg>(i), value, false)) {
            emu_.Get<Fatal>().Die("Tps65950Rtc: power-on value 0x%02X of RTC register 0x%02X is not a count", value,
                                  kSeconds + i);
        }
    }
    week_hi_ = tps65950::PowerRtcPowerOn(static_cast<uint8_t>(kSeconds + Cal::kWeeks)) & kWeeksReserved;
    for (uint32_t i = 0; i < Cal::kAlarmCount; ++i) {
        alarm_[i] = tps65950::PowerRtcPowerOn(static_cast<uint8_t>(kAlarmSeconds + i)) & kAlarmMask[i];
    }
    ctrl_          = tps65950::PowerRtcPowerOn(kCtrl) & kCtrlStored;
    round_pending_ = (tps65950::PowerRtcPowerOn(kCtrl) & kRound30s) != 0u;
    status_        = tps65950::PowerRtcPowerOn(kStatus);
    it_            = tps65950::PowerRtcPowerOn(kInterrupts);
    comp_[0]       = tps65950::PowerRtcPowerOn(kCompLsb);
    comp_[1]       = tps65950::PowerRtcPowerOn(kCompMsb);
    LatchTime();
    alarm_match_ = cal_.Matches(alarm_, Mode12());
    sec_start_   = osc_.Now();
    sec_len_     = SecondLength();
}

void Tps65950Rtc::LatchTime() {
    latch_ = cal_.Encode(Mode12());
    latch_[Cal::kWeeks] |= week_hi_;
}

bool Tps65950Rtc::Running() const { return (ctrl_ & kStopRtc) != 0u; }

bool Tps65950Rtc::Mode12() const { return (ctrl_ & kMode1224) != 0u; }

bool Tps65950Rtc::PerSecondWork() const {
    return round_pending_ || (ctrl_ & kAutoComp) != 0u || (it_ & (kItTimer | kItAlarm)) != 0u;
}

int64_t Tps65950Rtc::CompValue() const {
    return static_cast<int16_t>(static_cast<uint16_t>((comp_[1] << 8) | comp_[0]));
}

/* TPS65950 TRM SWCU050E page 122: with AUTO_COMP, COMP_REG is added to the 32-kHz counter each hour. */
uint64_t Tps65950Rtc::SecondLength() const {
    if ((ctrl_ & kAutoComp) == 0u || !cal_.AtHour()) return kTicksPerSecond;
    if (CompValue() == kCompForbidden) {
        emu_.Get<Fatal>().Die("Tps65950Rtc: AUTO_COMP applies the forbidden RTC_COMP value 0x7FFF");
    }
    return static_cast<uint64_t>(static_cast<int64_t>(kTicksPerSecond) - CompValue());
}

Tps65950Rtc::Cal Tps65950Rtc::AtNextBoundary(uint8_t& cross) const {
    Cal next = cal_;
    cross    = StepBoundary(next, round_pending_);
    return next;
}

uint64_t Tps65950Rtc::SecondStart(uint64_t n) const {
    const uint64_t start = sec_start_ + sec_len_ + (n - 1u) * kTicksPerSecond;
    if ((ctrl_ & kAutoComp) == 0u) return start;
    uint8_t        cross = 0;
    const Cal      next  = AtNextBoundary(cross);
    const uint64_t first = next.AtHour() ? 1u : 1u + next.SecondsToNextHour();
    if (n <= first) return start;
    const int64_t hours = static_cast<int64_t>(1u + (n - 1u - first) / 3600u);
    return static_cast<uint64_t>(static_cast<int64_t>(start) - hours * CompValue());
}

void Tps65950Rtc::Advance() {
    const uint64_t now = osc_.Now();
    if (now < sec_start_ + sec_len_) return;
    if (!PerSecondWork()) {
        const uint64_t extra = (now - sec_start_ - sec_len_) / kTicksPerSecond;
        sec_start_ += sec_len_ + extra * kTicksPerSecond;
        sec_len_ = kTicksPerSecond;
        cal_.Advance(extra + 1u);
        alarm_match_ = cal_.Matches(alarm_, Mode12());
        return;
    }
    while (now >= sec_start_ + sec_len_) {
        sec_start_ += sec_len_;
        const uint8_t cross = StepBoundary(cal_, round_pending_);
        round_pending_      = false;
        sec_len_            = SecondLength();
        OnSecond(cross);
    }
}

/* TPS65950 TRM SWCU050E page 122 and Table 3-2: the periodic interrupt sets 1S_EVENT and every longer event concurrent
   with it; EVERY selects the period. */
bool Tps65950Rtc::PeriodicAt(uint8_t cross) const {
    if ((it_ & kItTimer) == 0u) return false;
    constexpr uint8_t kPeriodCross[4] = {0u, Cal::kCrossMinute, Cal::kCrossHour, Cal::kCrossDay};
    const uint8_t     every           = it_ & kEvery;
    return every == 0u || (cross & kPeriodCross[every]) != 0u;
}

void Tps65950Rtc::OnSecond(uint8_t cross) {
    if (PeriodicAt(cross)) {
        status_ |= k1sEvent;
        if ((cross & Cal::kCrossMinute) != 0u) status_ |= k1mEvent;
        if ((cross & Cal::kCrossHour) != 0u) status_ |= k1hEvent;
        if ((cross & Cal::kCrossDay) != 0u) status_ |= k1dEvent;
    }
    Compare();
}

void Tps65950Rtc::Compare() {
    const bool match = cal_.Matches(alarm_, Mode12());
    if (match && !alarm_match_ && (it_ & kItAlarm) != 0u && (status_ & kAlarm) == 0u) {
        status_ |= kAlarm;
        LOG(Periph, "[TPS65950] RTC alarm at %02X:%02X:%02X %02X/%02X/%02X\n", alarm_[2], alarm_[1], alarm_[0],
            alarm_[3], alarm_[4], alarm_[5]);
    }
    alarm_match_ = match;
}

bool Tps65950Rtc::RtcIt() const {
    return ((status_ & kAlarm) != 0u && (it_ & kItAlarm) != 0u) ||
           ((status_ & kEvents) != 0u && (it_ & kItTimer) != 0u);
}

void Tps65950Rtc::Publish() {
    const bool level = RtcIt();
    if (level == rtc_it_) return;
    rtc_it_ = level;
    interrupts_->SetRtcIt(level);
}

uint64_t Tps65950Rtc::NextInterruptSecond() const {
    uint8_t   cross = 0;
    const Cal next  = AtNextBoundary(cross);
    uint64_t  n     = kNever;
    if ((it_ & kItTimer) != 0u) n = PeriodicAt(cross) ? 1u : 1u + next.SecondsToBoundary(it_ & kEvery);
    if ((it_ & kItAlarm) == 0u) return n;
    uint64_t secs = 0;
    if (next.Matches(alarm_, Mode12()) && !alarm_match_) return 1u;
    if (next.SecondsToAlarm(alarm_, Mode12(), secs)) n = std::min(n, 1u + secs);
    return n;
}

void Tps65950Rtc::Arm() {
    const uint64_t n = Running() && !RtcIt() ? NextInterruptSecond() : kNever;
    if (n == kNever) {
        clock_->Disarm(event_);
        return;
    }
    osc_.ArmAt(event_, SecondStart(n));
}

void Tps65950Rtc::OnEvent() {
    Advance();
    Publish();
    Arm();
}

uint8_t Tps65950Rtc::Read(uint8_t sub) {
    const bool round = round_pending_;
    const bool line  = rtc_it_;
    Advance();
    uint8_t value = 0;
    if (sub < kAlarmSeconds) {
        value = latch_[sub - kSeconds];
    } else if (sub < kCtrl) {
        value = alarm_[sub - kAlarmSeconds];
    } else {
        switch (sub) {
            case kCtrl:       value = static_cast<uint8_t>(ctrl_ | (round_pending_ ? kRound30s : 0u)); break;
            case kStatus:     ReadStatusClear(value); break;
            case kInterrupts: value = it_; break;
            case kCompLsb:    value = comp_[0]; break;
            case kCompMsb:    value = comp_[1]; break;
        }
    }
    Publish();
    if (round != round_pending_ || line != rtc_it_) Arm();
    return value;
}

/* TPS65950 TRM SWCU050E page 125, section 3.5.3: after the timer bits are read they must be reset to 0, and
   Table 3-60 (page 145) types them R. */
void Tps65950Rtc::ReadStatusClear(uint8_t& value) {
    value = static_cast<uint8_t>(status_ | (Running() ? kRun : 0u));
    status_ &= static_cast<uint8_t>(~kEvents);
}

void Tps65950Rtc::Write(uint8_t sub, uint8_t value) {
    Advance();
    if (sub < kAlarmSeconds) {
        WriteTime(sub, value);
    } else if (sub < kCtrl) {
        alarm_[sub - kAlarmSeconds] = value & kAlarmMask[sub - kAlarmSeconds];
        Compare();
    } else {
        switch (sub) {
            case kCtrl:       WriteCtrl(value); break;
            case kStatus:     status_ &= static_cast<uint8_t>(~(value & (kPowerUp | kAlarm))); break;
            case kInterrupts: it_ = value; break;
            case kCompLsb:    comp_[0] = value; break;
            case kCompMsb:    comp_[1] = value; break;
        }
    }
    Publish();
    Arm();
}

/* TPS65950 TRM SWCU050E page 121: the TC registers can be written without stopping the RTC. */
void Tps65950Rtc::WriteTime(uint8_t sub, uint8_t value) {
    const auto reg = static_cast<Cal::Reg>(sub - kSeconds);
    if (!cal_.Write(reg, value, Mode12())) {
        emu_.Get<Fatal>().Die("Tps65950Rtc: write 0x%02X to RTC register 0x%02X is not a count of that field", value,
                              sub);
    }
    if (reg == Cal::kWeeks) week_hi_ = value & kWeeksReserved;
    Compare();
}

/* TPS65950 TRM SWCU050E Table 3-58 (page 144): ROUND_30S reads 1 until the rounding; MODE_12_24 switches without
   disturbing the RTC. Linux rtc-twl twl_rtc_read_time sets GET_TIME, then reads the time registers. */
void Tps65950Rtc::WriteCtrl(uint8_t value) {
    auto& fatal = emu_.Get<Fatal>();
    if ((value & kTestMode) != 0u) fatal.Die("Tps65950Rtc: RTC_CTRL 0x%02X sets TEST_MODE; not modelled", value);
    if ((value & kSet32Counter) != 0u) {
        fatal.Die("Tps65950Rtc: RTC_CTRL 0x%02X sets SET_32_COUNTER; not modelled", value);
    }
    const bool was_running = Running();
    ctrl_                  = value & kCtrlStored;
    if ((value & kRound30s) != 0u) round_pending_ = true;
    if (Running() != was_running) {
        osc_.SetCounting(Running());
        LOG(Periph, "[TPS65950] RTC %s\n", Running() ? "running" : "frozen");
    }
    if ((value & kGetTime) != 0u) {
        LatchTime();
        LOG(Periph, "[TPS65950] RTC GET_TIME %02X:%02X:%02X %02X/%02X/%02X\n", latch_[Cal::kHours],
            latch_[Cal::kMinutes], latch_[Cal::kSeconds], latch_[Cal::kDays], latch_[Cal::kMonths],
            latch_[Cal::kYears]);
    }
    Compare();
}

void Tps65950Rtc::Save(StateWriter& w) {
    osc_.Save(w, "tps_rtc_");
    cal_.Save(w);
    w.WriteBytes("tps_rtc_latch", latch_.data(), latch_.size());
    w.WriteBytes("tps_rtc_alarm", alarm_.data(), alarm_.size());
    w.Write("tps_rtc_ctrl", ctrl_);
    w.Write("tps_rtc_status", status_);
    w.Write("tps_rtc_interrupts", it_);
    w.WriteBytes("tps_rtc_comp", comp_, sizeof(comp_));
    w.Write("tps_rtc_week_hi", week_hi_);
    w.Write<uint64_t>("tps_rtc_sec_start", sec_start_);
    w.Write<uint64_t>("tps_rtc_sec_len", sec_len_);
    w.Write("tps_rtc_round_pending", round_pending_);
    w.Write("tps_rtc_alarm_match", alarm_match_);
    w.Write("tps_rtc_it", rtc_it_);
}

void Tps65950Rtc::Restore(StateReader& r) {
    osc_.Restore(r, "tps_rtc_");
    cal_.Restore(r);
    r.ReadBytes("tps_rtc_latch", latch_.data(), latch_.size());
    r.ReadBytes("tps_rtc_alarm", alarm_.data(), alarm_.size());
    r.Read("tps_rtc_ctrl", ctrl_);
    r.Read("tps_rtc_status", status_);
    r.Read("tps_rtc_interrupts", it_);
    r.ReadBytes("tps_rtc_comp", comp_, sizeof(comp_));
    r.Read("tps_rtc_week_hi", week_hi_);
    r.Read("tps_rtc_sec_start", sec_start_);
    r.Read("tps_rtc_sec_len", sec_len_);
    r.Read("tps_rtc_round_pending", round_pending_);
    r.Read("tps_rtc_alarm_match", alarm_match_);
    r.Read("tps_rtc_it", rtc_it_);
    clock_->Disarm(event_);
}

void Tps65950Rtc::PostRestore() {
    Arm();
}

REGISTER_SERVICE(Tps65950Rtc);

}
