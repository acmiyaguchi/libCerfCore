#include "oscillator_ticks.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include "../core/tick_scale.h"
#include <cerfcore/host/guest_deep_sleep.h>
#include <cerfcore/state/state_stream.h>

#include <string>

namespace {

constexpr uint64_t kNsPerSec   = 1000000000u;
constexpr uint64_t kMaxParkDen = static_cast<uint64_t>(INT64_MAX) / kNsPerSec;

}

void OscillatorTicks::RequireOscRate(uint64_t num, uint64_t den) const {
    if (num != 0u && den != 0u && (!credits_park_ || den <= kMaxParkDen)) return;
    emu_.Get<Fatal>().Die("OscillatorTicks: the oscillator rate %llu/%llu Hz",
                          static_cast<unsigned long long>(num),
                          static_cast<unsigned long long>(den));
}

void OscillatorTicks::Attach(uint64_t osc_num, uint64_t osc_den) {
    RequireOscRate(osc_num, osc_den);
    clock_   = &emu_.Get<GuestCycleClock>();
    sleep_   = &emu_.Get<GuestDeepSleep>();
    osc_num_ = osc_num;
    osc_den_ = osc_den;
    Rebase();
}

uint64_t OscillatorTicks::ClockCycles() { return clock_->Cycles(); }

uint64_t OscillatorTicks::Scale() const { return kNsPerSec * osc_den_; }

void OscillatorTicks::SetRatio() {
    ticks_.SetRate(clock_->ClockRate(), OscRate());
}

uint64_t OscillatorTicks::CreditNs(uint64_t ns) {
    const uint64_t scale = Scale();
    const uint64_t whole = ns / scale;
    const uint64_t part  = ns % scale;
    uint64_t       rem   = 0u;
    const uint64_t quot  = MulDivU64(part, osc_num_, scale, rem);
    const uint64_t sum   = rem + credit_rem_;
    const uint64_t carry = quot + (sum >= scale ? 1u : 0u);
    if (whole > (UINT64_MAX - carry) / osc_num_ || !ticks_.AddTicks(whole * osc_num_ + carry)) {
        emu_.Get<Fatal>().Die("OscillatorTicks: a %llu ns credit overflows the tick count",
                              static_cast<unsigned long long>(ns));
    }
    credit_rem_ = sum >= scale ? sum - scale : sum;
    return whole * osc_num_ + carry;
}

void OscillatorTicks::DrainPark() {
    if (!credits_park_) return;
    const int64_t total = sleep_->SleptNs();
    if (total == slept_seen_) return;
    const uint64_t ns = static_cast<uint64_t>(total - slept_seen_);
    slept_seen_       = total;
    if (ClockStopped()) return;
    park_ticks_ += CreditNs(ns);
}

uint64_t OscillatorTicks::CycleOf(uint64_t tick) {
    if (ClockStopped()) {
        emu_.Get<Fatal>().Die("OscillatorTicks: the cycle of a tick asked while the oscillator's "
                              "clock is stopped");
    }
    const uint64_t cycle = clock_->Cycles();
    if (tick <= Now()) return cycle;
    return ticks_.CycleOfTick(tick) + (cycle - ClockCycles());
}

void OscillatorTicks::ArmAt(GuestCycleClock::Event* event, uint64_t tick) {
    if (ClockStopped()) {
        clock_->Disarm(event);
        return;
    }
    clock_->Arm(event, CycleOf(tick));
}

int64_t OscillatorTicks::SleptNsAtTick(uint64_t tick) {
    if (!credits_park_) {
        emu_.Get<Fatal>().Die("OscillatorTicks: a park wake due on a counter that stops in the park");
    }
    const uint64_t now = Now();
    if (tick <= now) return slept_seen_;
    const uint64_t scale = Scale();
    const uint64_t ticks = tick - now;
    const uint64_t whole = ticks / osc_num_;
    const uint64_t pt    = ticks % osc_num_;
    uint64_t       r     = 0u;
    const uint64_t q     = MulDivU64(pt, scale, osc_num_, r);
    const int64_t  part_ns =
        r >= credit_rem_
            ? static_cast<int64_t>(q + (r > credit_rem_ ? 1u : 0u))
            : static_cast<int64_t>(q) - static_cast<int64_t>((credit_rem_ - r) / osc_num_);
    const uint64_t headroom =
        static_cast<uint64_t>(GuestDeepSleep::kNoParkWake - slept_seen_);
    if (whole > headroom / scale) return GuestDeepSleep::kNoParkWake;
    const uint64_t whole_ns = whole * scale;
    if (part_ns > 0 && whole_ns > headroom - static_cast<uint64_t>(part_ns))
        return GuestDeepSleep::kNoParkWake;
    return slept_seen_ + static_cast<int64_t>(whole_ns) + part_ns;
}

void OscillatorTicks::RescaleAt(uint64_t now) {
    ticks_.Rescale(now, clock_->ClockRate(), OscRate());
}

void OscillatorTicks::Rescale() { RescaleAt(ClockCycles()); }

void OscillatorTicks::SetOscRate(uint64_t osc_num, uint64_t osc_den) {
    if (osc_num == osc_num_ && osc_den == osc_den_) return;
    RequireOscRate(osc_num, osc_den);
    DrainPark();
    const uint64_t now     = ClockCycles();
    const uint64_t old_den = osc_den_;
    osc_num_    = osc_num;
    osc_den_    = osc_den;
    credit_rem_ = MulDivU64(credit_rem_, osc_den, old_den);
    RescaleAt(now);
}

void OscillatorTicks::Rebase() {
    SetRatio();
    ticks_.Start(ClockCycles());
    park_ticks_ = 0;
    credit_rem_   = 0;
    slept_seen_ = sleep_->SleptNs();
}

void OscillatorTicks::Save(StateWriter& w, const char* prefix) {
    DrainPark();
    const std::string p(prefix);
    const uint64_t    now = ClockCycles();
    w.Write<uint64_t>((p + "osc_num").c_str(), osc_num_);
    w.Write<uint64_t>((p + "osc_den").c_str(), osc_den_);
    w.Write<uint64_t>((p + "osc_ticks").c_str(), ticks_.TicksAt(now));
    ticks_.TickPhaseAt(now).Save(w, (p + "osc_tick_phase").c_str());
    w.Write<uint64_t>((p + "osc_credit_rem").c_str(), credit_rem_);
}

void OscillatorTicks::Restore(StateReader& r, const char* prefix) {
    const std::string           p(prefix);
    uint64_t                    osc_num = 0, osc_den = 0, ticks = 0, rem = 0;
    CycleAnchoredCounter::Phase phase;
    r.Read((p + "osc_num").c_str(), osc_num);
    r.Read((p + "osc_den").c_str(), osc_den);
    osc_num_ = osc_num;
    osc_den_ = osc_den;
    r.Read((p + "osc_ticks").c_str(), ticks);
    phase.Restore(r, (p + "osc_tick_phase").c_str());
    r.Read((p + "osc_credit_rem").c_str(), rem);
    SetRatio();
    ticks_.PlaceAtPhase(ClockCycles(), ticks, phase);
    park_ticks_ = 0;
    credit_rem_   = rem;
    slept_seen_ = sleep_->SleptNs();
}

uint64_t StoppableOscillatorTicks::ClockCycles() {
    return (stopped_ ? stopped_at_ : clock_->Cycles()) - stopped_span_;
}

void StoppableOscillatorTicks::SetCounting(bool counting) {
    if (counting == !stopped_) return;
    DrainPark();
    const uint64_t now = clock_->Cycles();
    if (counting) stopped_span_ += now - stopped_at_;
    else          stopped_at_ = now;
    stopped_ = !counting;
}

void StoppableOscillatorTicks::Save(StateWriter& w, const char* prefix) {
    w.Write<uint8_t>((std::string(prefix) + "osc_counting").c_str(), stopped_ ? 0u : 1u);
    OscillatorTicks::Save(w, prefix);
}

void StoppableOscillatorTicks::Restore(StateReader& r, const char* prefix) {
    uint8_t counting = 1u;
    r.Read((std::string(prefix) + "osc_counting").c_str(), counting);
    stopped_      = counting == 0u;
    stopped_at_   = clock_->Cycles();
    stopped_span_ = 0u;
    OscillatorTicks::Restore(r, prefix);
}
