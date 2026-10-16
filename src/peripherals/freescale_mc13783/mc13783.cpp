#include "mc13783.h"

#include "mc13783_adc.h"
#include "mc13783_int_line.h"
#include <cerfcore/boards/board_context.h>
#include "../../boards/toshiba_gigabeat_s/toshiba_gigabeat_s_family_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/host/guest_deep_sleep.h>
#include "../../execution/guest_cycle_clock.h"
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>

#include <cstring>

namespace cerfcore {

REGISTER_SERVICE(Mc13783);

namespace {

constexpr uint32_t kRegStatus0     = 0;
constexpr uint32_t kRegMask0       = 1;
constexpr uint32_t kRegSense0      = 2;
constexpr uint32_t kRegStatus1     = 3;
constexpr uint32_t kRegMask1       = 4;
constexpr uint32_t kRegSense1      = 5;
constexpr uint32_t kRegArbitration = 9;
constexpr uint32_t kRegRtcTime     = 20;
constexpr uint32_t kRegRtcAlarm    = 21;
constexpr uint32_t kRegRtcDay      = 22;
constexpr uint32_t kRegRtcDayAlarm = 23;
constexpr uint32_t kFieldMask      = 0x00FFFFFFu;
constexpr uint32_t kSecondsPerDay  = 86400u;

/* MC13783 UG Table 13-4 and Table 13-5. */
constexpr uint32_t kStatus1Hzi    = 1u << 0;
constexpr uint32_t kStatus1Todai  = 1u << 1;
constexpr uint32_t kStatus1Rtcrst = 1u << 7;
constexpr uint32_t kMask1Hzm      = 1u << 0;
constexpr uint32_t kMask1Todam    = 1u << 1;

/* MC13783 UG Table 13-10 (page 13-11): register 9 ADCSEL 13:12. */
constexpr uint32_t kAdcselShift = 12u;

/* MC13783 UG Table 13-21, Table 13-22 and Table 13-24. */
constexpr uint32_t kTodMask  = 0x1FFFFu;
constexpr uint32_t kTodaMask = 0x1FFFFu;
constexpr uint32_t kDayaMask = 0x7FFFu;

/* UG Table 13-3 BPONS, LOBATHS, IDFLOATS; Table 10-8: IDFLOATS with no device. */
constexpr uint32_t kSense0 = (1u << 12) | (1u << 14) | (1u << 19);

/* UG Table 13-6 ONOFD1S, ONOFD2S, ONOFD3S, CLKS; Table 3-8: ONOFDnS is 1 while the pin is
   high. */
constexpr uint32_t kSense1 = (1u << 3) | (1u << 4) | (1u << 5) | (1u << 14);

/* MC13783 datasheet §4.1.2.1: the part generates a 32.768 kHz clock; §4.1.2.2.1 divides
   it down to the 1 Hz time tick. */
constexpr uint64_t kClk32kHz = 32768u;

/* MC13783 UG page 13-4: "Default: The value after reset. If an * is indicated then the value depends on
   the power up mode setting"; Table 5-9 (page 5-15): VIOHI, VIOLO 1.8 V and VRF1 2.775 V are on in every
   valid PUMS setting; "Off indicates the regulator is not enabled and the regulator setting bits are set to 0." */
constexpr uint32_t kPowerOnReset[64] = {
    0xE9EFFFu, 0xE9EFFFu, 0x000000u, 0xFFF83Cu,
    0xFFF83Cu, 0x000000u, 0x000000u, 0x000000u,
    0xFFFFFFu, 0x0FFFFFu, 0x01FFFFu, 0xFFFFFFu,
    0x3FFFFFu, 0x00FF88u, 0x000000u, 0x000000u,
    0xFFFFFFu, 0x000000u, 0x000000u, 0x000000u,
    0x000000u, 0x000000u, 0x000000u, 0x000000u,
    0xFFFFFFu, 0x03FFFFu, 0xFFFFFFu, 0x03FFFFu,
    0xFFFFFFu, 0xFFFFFFu, 0x0FFFFFu, 0x00FFFFu,
    0xFFFFFFu, 0xFFFFFFu, 0x03FFFFu, 0x000000u,
    0xFFFFFFu, 0x3FFFFFu, 0xFFFFFFu, 0x3FFFFFu,
    0xFFFFFFu, 0xFFFFFFu, 0x000000u, 0x000000u,
    0x000000u, 0x000000u, 0x000000u, 0x000000u,
    0xFFFFFFu, 0xFFFFFFu, 0x0001FFu, 0xFFFFFFu,
    0xFFFFFFu, 0xFFFFFFu, 0xFFFFFFu, 0xFFFFFFu,
    0xFFFFFFu, 0x000000u, 0x000000u, 0x000000u,
    0xFFFFFFu, 0xFFFFFFu, 0xFFFFFFu, 0xFFFFFFu,
};
constexpr uint32_t kResetDefault[64] = {
    0x000000u, 0xFFFFFFu, 0x000000u, 0x000000u,
    0xFFFFFFu, 0x000000u, 0x000000u, 0x000000u,
    0x000000u, 0x000000u, 0x000000u, 0x000000u,
    0x000000u, 0x000040u, 0x000000u, 0x000000u,
    0x200000u, 0x000000u, 0x000000u, 0x000000u,
    0x000000u, 0x01FFFFu, 0x000000u, 0x007FFFu,
    0x000000u, 0x000000u, 0x000000u, 0x000000u,
    0x220200u, 0x020200u, 0x00000Cu, 0x00000Cu,
    0x000048u, 0x001000u, 0x000000u, 0x000000u,
    0x001000u, 0x00D35Au, 0x420000u, 0x013060u,
    0x180027u, 0x0E0004u, 0x000000u, 0x000000u,
    0x000000u, 0x000000u, 0x000000u, 0x000000u,
    0x000000u, 0x080060u, 0x000006u, 0x000000u,
    0x000000u, 0x000000u, 0x000000u, 0x000000u,
    0x000000u, 0x000000u, 0x000000u, 0x000000u,
    0x000000u, 0x000000u, 0x000000u, 0x000000u,
};

/* MC13783 UG Tables 13-1 to 13-62 (pages 13-4 to 13-52), the R/W column: "Unused | R | -" bits and
   registers 6 and 7 are read-only; registers 58 / 59 TRIM are R/W. */
constexpr uint32_t kWritable[64] = {
    0xE9FFFFu, 0xE9FFFFu, 0x000000u, 0xFFFFFFu,
    0xFFFFFFu, 0x000000u, 0x000000u, 0x000000u,
    0xFFFFFFu, 0x0FFFFFu, 0x01FFFFu, 0xFFFFFFu,
    0x3FFFFFu, 0xFFFFFFu, 0x1FFFFFu, 0x000FFFu,
    0xFFFFFFu, 0x000000u, 0xFFFFFFu, 0xFFFFFFu,
    0x000000u, 0x01FFFFu, 0x000000u, 0x007FFFu,
    0xFFFFFFu, 0x03FFFFu, 0xFFFFFFu, 0x03FFFFu,
    0xFFFFFFu, 0xFFFFFFu, 0x0FFFFFu, 0x00FFFFu,
    0xFFFFFFu, 0xFFFFFFu, 0x03FFFFu, 0x000000u,
    0xFFFFFFu, 0x3FFFFFu, 0xFFFFFFu, 0x3FFFFFu,
    0xFFFFFFu, 0xFFFFFFu, 0x000000u, 0x000000u,
    0x000000u, 0x000000u, 0x000000u, 0x000000u,
    0xFFFFFFu, 0xFFFFFFu, 0x0001FFu, 0xFFFFFFu,
    0xFFFFFFu, 0xFFFFFFu, 0xFFFFFFu, 0xFFFFFFu,
    0xFFFFFFu, 0x000000u, 0xFFFFFFu, 0xFFFFFFu,
    0xFFFFFFu, 0xFFFFFFu, 0xFFFFFFu, 0xFFFFFFu,
};

}

bool Mc13783::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::ToshibaGigabeatS;
}

void Mc13783::OnReady() {
    clock_       = &emu_.Get<GuestCycleClock>();
    alarm_event_ = clock_->Add([this] { OnAlarm(); });
    hz_event_    = clock_->Add([this] { UpdateInt(); });
    int_line_    = &emu_.Get<Mc13783IntLine>();

    for (uint32_t r = 0; r < kRegisterCount; ++r) regs_[r] = kResetDefault[r];
    /* UG §4.2.3: RTCRSTI defaults to 1 on RTCPORB. */
    regs_[kRegStatus1] |= kStatus1Rtcrst;

    clk32k_.Attach(kClk32kHz, 1u);
    adc_ = &emu_.Get<Mc13783Adc>();
    adc_->SetStatusSink([this](uint32_t bits) { RaiseStatus0(bits); });
    clock_->RegisterRateListener([this] {
        clk32k_.Rescale();
        RearmAlarm(clock_->Cycles());
        RearmHz();
    });
    auto& sleep = emu_.Get<GuestDeepSleep>();
    sleep.RegisterParkClock([this] {
        RearmAlarm(clock_->Cycles());
        RearmHz();
    });
    sleep.RegisterPowerOnWake();
    sleep.RegisterParkWakeSource([this] {
        return AlarmTurnOnArmed() && clk32k_.Now() >= off_alarm_tick_;
    });
    sleep.RegisterParkWakeDue([this] {
        return AlarmTurnOnArmed() ? clk32k_.SleptNsAtTick(off_alarm_tick_)
                                  : GuestDeepSleep::kNoParkWake;
    });
    emu_.Get<GuestCpuReset>().RegisterResetReleaseListener([this] {
        if (!on_) PowerOn();
    });
}

/* MC13783 datasheet Table 8 (page 22): On "To stay in this mode, the WDI pin has to be high and remain
   high. If not, the part will transition to Off mode."; UG Figure 5-1 (page 5-3): "WDI has influence
   only in the 'On' state". */
void Mc13783::SetWdi(bool high) {
    if (high == wdi_high_) return;
    wdi_high_ = high;
    if (!high && on_) PowerOff();
}

void Mc13783::PowerOff() {
    on_             = false;
    off_alarm_tick_ = regs_[kRegRtcAlarm] < kSecondsPerDay ? NextAlarmTick() : kNoAlarmTick;
    LOG(Periph, "[MC13783] WDI low: Off\n");
    emu_.Get<GuestDeepSleep>().Enter();
}

/* UG page 5-10: "Masking the interrupts related to the turn on events will not prevent the part to turn
   on except for the time of day alarm". */
bool Mc13783::AlarmTurnOnArmed() const {
    return !on_ && off_alarm_tick_ != kNoAlarmTick && (regs_[kRegMask1] & kMask1Todam) == 0u;
}

/* MC13783 datasheet Table 8 (page 22): Off "Only the MC13783 core circuitry at VATLAS and the RTC module
   are powered"; UG page 5-4, Cold Start: "The reset signals RESETB and RESETBMCU are kept low." */
void Mc13783::PowerOn() {
    const bool matched = off_alarm_tick_ != kNoAlarmTick && clk32k_.Now() >= off_alarm_tick_;
    for (uint32_t r = 0; r < kRegisterCount; ++r) {
        regs_[r] = (regs_[r] & ~kPowerOnReset[r]) | (kResetDefault[r] & kPowerOnReset[r]);
    }
    adc_->Reset();
    if (matched) regs_[kRegStatus1] |= kStatus1Todai;
    on_             = true;
    off_alarm_tick_ = kNoAlarmTick;
    pending_todai_  = false;
    alarm_tick_     = kNoAlarmTick;
    LOG(Periph, "[MC13783] turn-on: On (alarm match %d)\n", matched ? 1 : 0);
    RearmAlarm(clock_->Cycles());
    RearmHz();
    UpdateInt();
}

void Mc13783::SaveState(StateWriter& w) {
    uint32_t regs[kRegisterCount];
    for (uint32_t r = 0; r < kRegisterCount; ++r) regs[r] = regs_[r];
    if (AlarmDue()) regs[kRegStatus1] |= kStatus1Todai;
    w.WriteBytes("regs", regs, sizeof(regs));
    clk32k_.Save(w, "clk32k_");
    w.Write("rtc_offset_secs", rtc_offset_secs_);
    w.Write("hz_clear_second", hz_clear_second_);
    w.Write<uint8_t>("pmic_on", on_ ? 1u : 0u);
    w.Write<uint8_t>("wdi_high", wdi_high_ ? 1u : 0u);
    w.Write("off_alarm_tick", off_alarm_tick_);
    adc_->Save(w);
}

void Mc13783::RestoreState(StateReader& r) {
    uint8_t on = 0, wdi = 0;
    r.ReadBytes("regs", regs_, sizeof(regs_));
    clk32k_.Restore(r, "clk32k_");
    r.Read("rtc_offset_secs", rtc_offset_secs_);
    r.Read("hz_clear_second", hz_clear_second_);
    r.Read("pmic_on", on);
    r.Read("wdi_high", wdi);
    r.Read("off_alarm_tick", off_alarm_tick_);
    on_       = on != 0u;
    wdi_high_ = wdi != 0u;
    adc_->Restore(r);
    pending_todai_ = false;
    alarm_tick_    = kNoAlarmTick;
    clock_->Disarm(alarm_event_);
    clock_->Disarm(hz_event_);
}

void Mc13783::PostRestore() {
    RearmAlarm(clock_->Cycles());
    RearmHz();
    int_asserted_ = IntLevel();
    int_line_->SetMc13783IntAsserted(int_asserted_);
}

uint64_t Mc13783::TickSeconds() {
    return clk32k_.Now() / kClk32kHz;
}

uint64_t Mc13783::RtcSeconds() {
    return static_cast<uint64_t>(static_cast<int64_t>(TickSeconds()) + rtc_offset_secs_);
}

/* §4.1.2.2.1: the 1 Hz tick drives the TOD counter, which rolls over into DAY at 86,400. */
void Mc13783::WriteRtc(uint32_t addr, uint32_t data) {
    const uint64_t now  = RtcSeconds();
    uint64_t       day  = now / kSecondsPerDay;
    uint64_t       tod  = now % kSecondsPerDay;
    if (addr == kRegRtcTime) {
        if ((data & kTodMask) >= kSecondsPerDay) return;
        tod = data & kTodMask;
    } else {
        day = data & kRtcDayMask;
    }
    rtc_offset_secs_ += static_cast<int64_t>(day * kSecondsPerDay + tod) -
                        static_cast<int64_t>(now);
}

/* UG §4.2.2: TODAI when the TOD counter equals TODA and the DAY counter equals DAYA. */
bool Mc13783::AlarmMatches(uint64_t secs) const {
    return secs % kSecondsPerDay == regs_[kRegRtcAlarm] &&
           ((secs / kSecondsPerDay) & kDayaMask) == regs_[kRegRtcDayAlarm];
}

bool Mc13783::AlarmDue() {
    return alarm_tick_ != kNoAlarmTick && clk32k_.Now() >= alarm_tick_;
}

void Mc13783::LatchDueAlarm() {
    if (!AlarmDue()) return;
    alarm_tick_    = kNoAlarmTick;
    pending_todai_ = true;
}

void Mc13783::RearmAlarm(uint64_t now) {
    const bool masked = (regs_[kRegMask1] & kMask1Todam) != 0u;
    LatchDueAlarm();
    if (pending_todai_ && masked) {
        pending_todai_ = false;
        regs_[kRegStatus1] |= kStatus1Todai;
    }
    if (pending_todai_) {
        alarm_tick_ = kNoAlarmTick;
        clock_->Arm(alarm_event_, now);
        return;
    }
    alarm_tick_ = regs_[kRegRtcAlarm] < kSecondsPerDay ? NextAlarmTick() : kNoAlarmTick;
    if (alarm_tick_ == kNoAlarmTick || masked) {
        clock_->Disarm(alarm_event_);
        return;
    }
    clk32k_.ArmAt(alarm_event_, alarm_tick_);
}

uint64_t Mc13783::NextAlarmTick() {
    constexpr uint64_t kPeriod = (uint64_t{kDayaMask} + 1u) * kSecondsPerDay;
    const uint64_t target = uint64_t{regs_[kRegRtcDayAlarm]} * kSecondsPerDay +
                            regs_[kRegRtcAlarm];
    uint64_t ahead = (target + kPeriod - RtcSeconds() % kPeriod) % kPeriod;
    if (ahead == 0u) ahead = kPeriod;
    return (TickSeconds() + ahead) * kClk32kHz;
}

/* UG Table 3-8: TODAI triggers on the low-to-high transition of the match. */
void Mc13783::OnAlarm() {
    pending_todai_ = false;
    alarm_tick_    = kNoAlarmTick;
    regs_[kRegStatus1] |= kStatus1Todai;
    UpdateInt();
    RearmAlarm(clock_->Cycles());
}

uint32_t Mc13783::Status1() {
    return regs_[kRegStatus1] | (TickSeconds() > hz_clear_second_ ? kStatus1Hzi : 0u) |
           (AlarmDue() ? kStatus1Todai : 0u);
}

/* MC13783 UG section 4.2.1 (page 4-3): "The 1 Hz timetick can be used to generate an 1HZI interrupt. The
   1HZI can be masked with corresponding 1HZM mask bit."; Table 3-8 (page 3-9): 1HZI trigger L2H. */
void Mc13783::RearmHz() {
    if ((regs_[kRegMask1] & kMask1Hzm) != 0u || (Status1() & kStatus1Hzi) != 0u) {
        clock_->Disarm(hz_event_);
        return;
    }
    clk32k_.ArmAt(hz_event_, (hz_clear_second_ + 1u) * kClk32kHz);
}

uint32_t Mc13783::Adcsel() const { return (regs_[kRegArbitration] >> kAdcselShift) & 3u; }

void Mc13783::RaiseStatus0(uint32_t bits) {
    regs_[kRegStatus0] |= bits;
    UpdateInt();
}

/* UG §3.4.1: PRIINT is high while an unmasked status bit is set. */
bool Mc13783::IntLevel() {
    return (((regs_[kRegStatus0] & ~regs_[kRegMask0]) | (Status1() & ~regs_[kRegMask1])) & kFieldMask) != 0u;
}

void Mc13783::UpdateInt() {
    const bool level = IntLevel();
    if (level == int_asserted_) return;
    int_asserted_ = level;
    int_line_->SetMc13783IntAsserted(level);
}

uint32_t Mc13783::ReadReg(uint32_t addr) {
    if (Mc13783Adc::OwnsRegister(addr)) return adc_->Read(addr, Adcsel());
    switch (addr) {
        case kRegRtcTime:
            return static_cast<uint32_t>(RtcSeconds() % kSecondsPerDay);
        case kRegRtcDay:
            return static_cast<uint32_t>(RtcSeconds() / kSecondsPerDay) & kRtcDayMask;
        /* UG §3.4.1: a status bit latches whether or not it is masked. */
        case kRegStatus1:
            return Status1();
        case kRegSense0:
            return kSense0;
        case kRegSense1:
            return kSense1;
        default:
            return regs_[addr] & kFieldMask;
    }
}

void Mc13783::WriteReg(uint32_t addr, uint32_t data) {
    const uint64_t now = clock_->Cycles();
    LatchDueAlarm();
    const bool matched = AlarmMatches(RtcSeconds());

    switch (addr) {
        /* UG §3.4.1: writing 1 to a status bit clears it. */
        case kRegStatus0:
            regs_[addr] &= ~data;
            break;
        case kRegStatus1:
            if ((data & kStatus1Hzi) != 0u) hz_clear_second_ = TickSeconds();
            if ((data & kStatus1Todai) != 0u) pending_todai_ = false;
            regs_[addr] &= ~data;
            break;
        case kRegSense0:
        case kRegSense1:
            break;
        case kRegRtcTime:
        case kRegRtcDay:
            WriteRtc(addr, data);
            break;
        case kRegRtcAlarm:
            regs_[addr] = data & kTodaMask;
            break;
        case kRegRtcDayAlarm:
            regs_[addr] = data & kDayaMask;
            break;
        default:
            if (Mc13783Adc::OwnsRegister(addr)) {
                adc_->Write(addr, data, Adcsel());
                break;
            }
            regs_[addr] = (regs_[addr] & ~kWritable[addr]) | (data & kWritable[addr]);
            break;
    }

    if (!matched && AlarmMatches(RtcSeconds())) regs_[kRegStatus1] |= kStatus1Todai;
    UpdateInt();
    RearmAlarm(now);
    RearmHz();
}

uint32_t Mc13783::SpiExchange(uint32_t cmd) {
    const bool     write = ((cmd >> 31) & 1u) != 0;
    const uint32_t addr  = (cmd >> 25) & 0x3Fu;
    const uint32_t data  = cmd & kFieldMask;
    const uint32_t value = ReadReg(addr);

    if (write) {
        WriteReg(addr, data);
        LOG(Periph, "[MC13783] WRITE reg=%u (0x%02X) data=0x%06X\n",
            addr, addr, data);
        return value;
    }

    LOG(Periph, "[MC13783] READ  reg=%u (0x%02X) value=0x%06X\n",
        addr, addr, value);
    return value;
}

}
