#include "imx28_timrot_timer.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/state/state_stream.h>
#include "imx28_icoll.h"

#include <string>

namespace cerfcore {

namespace {

/* Section 23.4.3 TIMCTRLn: IRQ [15], IRQ_EN [14], MATCH_MODE [11], POLARITY [8], UPDATE [7],
   RELOAD [6], PRESCALE [5:4], SELECT [3:0]; 23.4.15 TIMCTRL3 adds TEST_SIGNAL [19:16],
   DUTY_VALID [10], DUTY_CYCLE [9]. */
constexpr uint32_t kIrq          = 1u << 15;
constexpr uint32_t kIrqEn        = 1u << 14;
constexpr uint32_t kMatchMode    = 1u << 11;
constexpr uint32_t kDutyCycle    = 1u << 9;
constexpr uint32_t kPolarity     = 1u << 8;
constexpr uint32_t kUpdate       = 1u << 7;
constexpr uint32_t kReload       = 1u << 6;
constexpr uint32_t kPrescaleShift = 4u;
constexpr uint32_t kSelectMask   = 0xFu;
constexpr uint32_t kWritable     = 0x0000C9FFu;
constexpr uint32_t kWritableTimer3Extra = 0x000F0200u;

/* Section 23.4.3 SELECT: 0x0 never tick, 0x1-0xA PWM / rotary pins, 0xB 32 kHz, 0xC 8 kHz,
   0xD 4 kHz, 0xE 1 kHz, 0xF always tick. */
constexpr uint32_t kSelNever  = 0x0u;
constexpr uint32_t kSelPinMax = 0xAu;
constexpr uint32_t kSel32k    = 0xBu;
constexpr uint32_t kSel1k     = 0xEu;
constexpr uint32_t kSelAlways = 0xFu;

/* Section 23.2.2: the free-running count "returns to its default start value 0xFFFF_FFFF". */
constexpr uint32_t kMatchModeStart = 0xFFFFFFFFu;

/* Table 5-1 sources 48-51: timer0_irq to timer3_irq. */
constexpr uint32_t kIrqBase = 48u;

}  /* namespace */

void Imx28TimrotTimer::Init(Imx28Icoll* icoll) {
    icoll_ = icoll;
    clock_ = &emu_.Get<GuestCycleClock>();
    event_ = clock_->Add([this] { OnEvent(); });
}

/* Sections 23.4.3-23.4.6 memory map: CTRL, RUNNING_COUNT, FIXED_COUNT, MATCH_COUNT all reset to
   0. */
void Imx28TimrotTimer::Reset() {
    clock_->Disarm(event_);
    ctrl_       = 0u;
    fixed_      = 0u;
    match_      = 0u;
    seg_value_  = 0u;
    seg_pos_    = 0u;
    eval_pos_   = 0u;
    anchor_pos_ = 0u;
    ticking_    = false;
    phase_      = CounterPhase{};
    UpdateLine(false);
}

bool Imx28TimrotTimer::MatchMode() const {
    return (ctrl_ & kMatchMode) != 0u;
}

uint64_t Imx28TimrotTimer::Pos(uint64_t now) const {
    return ticking_ ? anchor_pos_ + counter_.TicksSince(now) : anchor_pos_;
}

/* Section 23.2.1: RELOAD copies "the fixed count ... to the free-running counter", else "the
   timer stalls when it reaches 0"; 23.2.2: in match mode it "restores to 0xFFFF_FFFF" at 0. */
uint32_t Imx28TimrotTimer::ValueAt(uint64_t e) const {
    if (MatchMode()) return seg_value_ - static_cast<uint32_t>(e);
    if (e <= seg_value_) return seg_value_ - static_cast<uint32_t>(e);
    if ((ctrl_ & kReload) == 0u || fixed_ == 0u) return 0u;
    const uint64_t period = uint64_t{fixed_} + 1u;
    return fixed_ - static_cast<uint32_t>((e - seg_value_ - 1u) % period);
}

/* Section 23.2.1: "the actual value written to the FIXED_COUNT register field should be
   count_value - 1" for a periodic interrupt, a period of FIXED_COUNT + 1 counts; section
   23.2.2: "interrupt is triggered every time free running count equals match count". */
uint64_t Imx28TimrotTimer::NextHit(uint64_t e0) const {
    if (MatchMode()) {
        const uint64_t d = static_cast<uint32_t>(seg_value_ - match_);
        const uint64_t k = e0 >= d ? ((e0 - d) >> 32) + 1u : 0u;
        return d + (k << 32);
    }
    const bool reload = (ctrl_ & kReload) != 0u && fixed_ != 0u;
    uint64_t   first  = 0u;
    if (seg_value_ >= 1u)  first = seg_value_;
    else if (reload)       first = uint64_t{fixed_} + 1u;
    else                   return kNoHit;
    if (first > e0) return first;
    if (!reload) return kNoHit;
    const uint64_t period = uint64_t{fixed_} + 1u;
    return first + ((e0 - first) / period + 1u) * period;
}

void Imx28TimrotTimer::Settle(uint64_t now) {
    const uint64_t pos = Pos(now);
    if (pos > eval_pos_ && NextHit(eval_pos_ - seg_pos_) <= pos - seg_pos_) ctrl_ |= kIrq;
    eval_pos_ = pos;
}

void Imx28TimrotTimer::Rebase(uint64_t now) {
    Settle(now);
    seg_value_ = ValueAt(eval_pos_ - seg_pos_);
    seg_pos_   = eval_pos_;
}

/* MCIMX28RM Rev. 2 sections 23.2.1 and 23.4.3 PRESCALE. */
GuestCycleClock::Rate Imx28TimrotTimer::TickRate() const {
    if (!clocks_.operating) return GuestCycleClock::Rate{0u, 1u};
    if ((ctrl_ & kSelectMask) == kSelAlways) {
        return clocks_.xbus.Scaled(1u, 1u << ((ctrl_ >> kPrescaleShift) & 0x3u)).Reduced();
    }
    return GuestCycleClock::Rate{0u, 1u};
}

void Imx28TimrotTimer::Retime(const Imx28TimrotClocks& clocks) {
    clocks_ = clocks;
    Apply();
}

void Imx28TimrotTimer::Apply() {
    const uint64_t                now  = clock_->Cycles();
    const GuestCycleClock::Rate   rate = TickRate();
    const GuestCycleClock::Rate   core = clock_->ClockRate();
    Settle(now);
    if (rate.num == 0u) {
        if (ticking_) {
            anchor_pos_ = Pos(now);
            phase_      = counter_.TickPhaseAt(now);
            ticking_    = false;
        }
    } else if (!ticking_) {
        counter_.SetRate(core, rate);
        counter_.AnchorAt(now, 0u, phase_);
        ratio_core_ = core;
        ratio_tick_ = rate;
        ticking_    = true;
    } else if (rate != ratio_tick_ || core != ratio_core_) {
        const uint64_t pos = Pos(now);
        counter_.RescaleRate(now, core, rate);
        anchor_pos_ = pos - counter_.TicksSince(now);
        ratio_core_ = core;
        ratio_tick_ = rate;
    }
    Arm();
}

void Imx28TimrotTimer::Arm() {
    if (!ticking_ || (ctrl_ & kIrqEn) == 0u || (ctrl_ & kIrq) != 0u) {
        clock_->Disarm(event_);
        return;
    }
    const uint64_t hit = NextHit(eval_pos_ - seg_pos_);
    if (hit == kNoHit) {
        clock_->Disarm(event_);
        return;
    }
    clock_->Arm(event_, counter_.CycleOfTick(seg_pos_ + hit - anchor_pos_));
}

void Imx28TimrotTimer::OnEvent() {
    const uint64_t now = clock_->Cycles();
    Settle(now);
    UpdateLine(false);
    Arm();
}

void Imx28TimrotTimer::UpdateLine(bool force) {
    const bool level = (ctrl_ & kIrq) != 0u && (ctrl_ & kIrqEn) != 0u;
    if (level == line_ && !force) return;
    line_ = level;
    if (level) icoll_->AssertSource(kIrqBase + index_);
    else       icoll_->DeassertSource(kIrqBase + index_);
}

uint32_t Imx28TimrotTimer::Ctrl() {
    Settle(clock_->Cycles());
    return ctrl_;
}

uint32_t Imx28TimrotTimer::Running() {
    const uint64_t now = clock_->Cycles();
    Settle(now);
    return ValueAt(eval_pos_ - seg_pos_);
}

/* Section 23.2.2: an interrupt is triggered when the free-running count equals the match
   count. */
void Imx28TimrotTimer::CheckMatchNow() {
    if (MatchMode() && seg_value_ == match_) ctrl_ |= kIrq;
}

/* Section 23.4.3: "programming an undefined value will result in always tick behavior". */
void Imx28TimrotTimer::WriteCtrl(Imx28SctOp op, uint32_t value) {
    const uint64_t now = clock_->Cycles();
    Rebase(now);
    const uint32_t mask = kWritable | (index_ == 3u ? kWritableTimer3Extra : 0u);
    const uint32_t old  = ctrl_;
    const uint32_t next = (old & ~mask) | (Imx28ApplySct(op, old, value) & mask);
    const uint32_t sel  = next & kSelectMask;
    if (sel > kSelNever && sel <= kSelPinMax) {
        emu_.Get<Fatal>().Die("Imx28Timrot: timer %u SELECT %X PWM/rotary pin (CTRL %08X)",
                              index_, sel, next);
    }
    if (sel >= kSel32k && sel <= kSel1k) {
        emu_.Get<Fatal>().Die("Imx28Timrot: timer %u SELECT %X clk_32k (CTRL %08X)", index_, sel,
                              next);
    }
    if ((next & kPolarity) != 0u && sel != kSelNever) {
        emu_.Get<Fatal>().Die("Imx28Timrot: timer %u POLARITY set (CTRL %08X)", index_, next);
    }
    if (index_ == 3u && (next & kDutyCycle) != 0u) {
        emu_.Get<Fatal>().Die("Imx28Timrot: timer 3 DUTY_CYCLE set (CTRL %08X)", next);
    }
    if ((old & kMatchMode) != 0u && (next & kMatchMode) == 0u) {
        emu_.Get<Fatal>().Die("Imx28Timrot: timer %u MATCH_MODE cleared (CTRL %08X)", index_, next);
    }
    ctrl_ = next;
    if ((old & kMatchMode) == 0u && (next & kMatchMode) != 0u) {
        seg_value_ = kMatchModeStart;
        CheckMatchNow();
    }
    Apply();
    UpdateLine(false);
}

/* Section 23.4.5 and Table 23-1: UPDATE copies the written value into the running count at
   once; otherwise a reload picks it up the next time the count reaches 0. */
void Imx28TimrotTimer::WriteFixed(uint32_t value) {
    const uint64_t now = clock_->Cycles();
    Rebase(now);
    if (MatchMode() && (ctrl_ & kUpdate) != 0u) {
        emu_.Get<Fatal>().Die("Imx28Timrot: timer %u FIXED_COUNT written in match mode with "
                              "UPDATE set", index_);
    }
    fixed_ = value;
    if ((ctrl_ & kUpdate) != 0u) seg_value_ = value;
    Apply();
}

void Imx28TimrotTimer::WriteMatch(uint32_t value) {
    const uint64_t now = clock_->Cycles();
    Rebase(now);
    match_ = value;
    CheckMatchNow();
    Apply();
    UpdateLine(false);
}

void Imx28TimrotTimer::Save(StateWriter& w) {
    const uint64_t now = clock_->Cycles();
    Rebase(now);
    const std::string p = "t" + std::to_string(index_) + "_";
    w.Write((p + "ctrl").c_str(), ctrl_);
    w.Write((p + "fixed").c_str(), fixed_);
    w.Write((p + "match").c_str(), match_);
    w.Write((p + "value").c_str(), seg_value_);
    w.Write<uint8_t>((p + "line").c_str(), line_ ? 1u : 0u);
    (ticking_ ? counter_.TickPhaseAt(now) : phase_).Save(w, (p + "phase").c_str());
}

void Imx28TimrotTimer::Restore(StateReader& r) {
    const std::string p = "t" + std::to_string(index_) + "_";
    uint8_t line = 0;
    r.Read((p + "ctrl").c_str(), ctrl_);
    r.Read((p + "fixed").c_str(), fixed_);
    r.Read((p + "match").c_str(), match_);
    r.Read((p + "value").c_str(), seg_value_);
    r.Read((p + "line").c_str(), line);
    phase_.Restore(r, (p + "phase").c_str());
    line_       = line != 0u;
    seg_pos_    = 0u;
    eval_pos_   = 0u;
    anchor_pos_ = 0u;
    ticking_    = false;
    clock_->Disarm(event_);
}

void Imx28TimrotTimer::PostRestore(const Imx28TimrotClocks& clocks) {
    clocks_ = clocks;
    Apply();
    UpdateLine(true);
}

}
