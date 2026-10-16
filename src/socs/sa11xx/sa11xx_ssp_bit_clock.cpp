#include "sa11xx_ssp_bit_clock.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/boards/board_context.h>
#include "sa1110_id.h"
#include "sa1100_id.h"
#include <cerfcore/state/state_stream.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "sa11xx_external_clocks.h"
#include "sa11xx_ssp_regs.h"

using namespace sa11xx_ssp;

namespace {

/* §11.12.7.2: the 3.6864 MHz clock "is first divided by a fixed value of 2 and then by a
   programmable number between 1 and 256". */
constexpr uint64_t kInternalClockHz = 3686400u;

/* §11.12.10.6 (printed 11-161): "the user must also set bit 19 of the GPIO alternate function
   register (GAFR), and clear bit 19 of the GPIO pin direction register (GPDR)". */
constexpr uint32_t kSspClockPin = 19u;

}

bool Sa11xxSspBitClock::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && (bd->GetSocId() == SocId::Sa1110 || bd->GetSocId() == SocId::Sa1100);
}

void Sa11xxSspBitClock::OnReady() {
    clock_ = &emu_.Get<GuestCycleClock>();
    ext_   = &emu_.Get<Sa11xxExternalClocks>();
    reset_ = &emu_.Get<GuestCpuReset>();
    clock_->RegisterRateListener([this] { OnCpuRate(); });
    ext_->RegisterChangeListener([this] { Reevaluate(); });
}

void Sa11xxSspBitClock::SetListener(BeforeChange before, AfterChange after) {
    before_ = std::move(before);
    after_  = std::move(after);
}

void Sa11xxSspBitClock::RequireRatio(bool fits) const {
    if (!fits) emu_.Get<Fatal>().Die("Sa11xxSsp: bit rate overflows the cycle clock ratio");
}

/* §11.12.9.4: "BitRate = 3.6864x10^6 / (2x(SCR+1))"; Linux sa11xx_uda1341_set_samplerate pairs
   SSCR1 ExtClk with SSCR0_SerClkDiv, the GPIO 19 clock divided by 2(SCR+1). */
bool Sa11xxSspBitClock::Select(uint32_t sscr0, uint32_t sscr1, GuestCycleClock::Rate& rate) const {
    const uint64_t div = 2u * (((sscr0 >> kScrShift) & 0xFFu) + 1u);
    if ((sscr1 & kEcs) == 0u) {
        rate = GuestCycleClock::Rate{kInternalClockHz, div};
        return true;
    }
    GuestCycleClock::Rate in{};
    if (!ext_->Rate(kSspClockPin, in)) return false;
    rate = in.Scaled(1u, div);
    return true;
}

void Sa11xxSspBitClock::Start(uint64_t now, uint32_t sscr0, uint32_t sscr1) {
    sscr0_   = sscr0;
    sscr1_   = sscr1;
    started_ = true;
    held_    = TickPosition{};
    clocked_ = Select(sscr0, sscr1, rate_);
    if (!clocked_) return;
    RequireRatio(halves_.SetRate(clock_->ClockRate(), rate_.Scaled(2u, 1u)));
    halves_.Start(now);
}

void Sa11xxSspBitClock::Change(uint64_t now, uint32_t sscr0, uint32_t sscr1) {
    sscr0_ = sscr0;
    sscr1_ = sscr1;
    GuestCycleClock::Rate rate{};
    if (!Select(sscr0, sscr1, rate)) {
        if (clocked_) held_ = halves_.PositionAt(now);
        clocked_ = false;
        return;
    }
    if (clocked_) {
        RequireRatio(halves_.Rescale(now, clock_->ClockRate(), rate.Scaled(2u, 1u)));
    } else {
        RequireRatio(halves_.SetRate(clock_->ClockRate(), rate.Scaled(2u, 1u)) &&
                     halves_.PlaceAt(now, held_));
    }
    rate_    = rate;
    clocked_ = true;
}

void Sa11xxSspBitClock::Reevaluate() {
    if (!started_ || reset_->LineHeld()) return;
    GuestCycleClock::Rate rate{};
    const bool known = Select(sscr0_, sscr1_, rate);
    if (known == clocked_ && (!known || rate == rate_)) return;
    const uint64_t now = clock_->Cycles();
    before_(now);
    Change(now, sscr0_, sscr1_);
    after_(now);
}

void Sa11xxSspBitClock::OnCpuRate() {
    if (!started_ || !clocked_) return;
    const uint64_t now = clock_->Cycles();
    before_(now);
    RequireRatio(halves_.Rescale(now, clock_->ClockRate(), rate_.Scaled(2u, 1u)));
    after_(now);
}

void Sa11xxSspBitClock::Save(StateWriter& w) {
    const TickPosition pos = clocked_ ? halves_.PositionAt(clock_->Cycles()) : held_;
    w.Write<uint32_t>("bit_clock_sscr0", sscr0_);
    w.Write<uint32_t>("bit_clock_sscr1", sscr1_);
    w.Write<uint8_t>("bit_clock_started", started_ ? 1u : 0u);
    w.Write<uint8_t>("bit_clock_clocked", clocked_ ? 1u : 0u);
    w.Write<uint64_t>("bit_clock_rate_num", rate_.num);
    w.Write<uint64_t>("bit_clock_rate_den", rate_.den);
    w.Write<uint64_t>("bit_clock_half_ticks", pos.ticks);
    w.Write<uint64_t>("bit_clock_half_phase", pos.phase);
    w.Write<uint64_t>("bit_clock_half_phase_den", pos.phase_den);
}

void Sa11xxSspBitClock::Restore(StateReader& r) {
    uint8_t started = 0, clocked = 0;
    TickPosition pos;
    r.Read("bit_clock_sscr0", sscr0_);
    r.Read("bit_clock_sscr1", sscr1_);
    r.Read("bit_clock_started", started);
    r.Read("bit_clock_clocked", clocked);
    r.Read("bit_clock_rate_num", rate_.num);
    r.Read("bit_clock_rate_den", rate_.den);
    r.Read("bit_clock_half_ticks", pos.ticks);
    r.Read("bit_clock_half_phase", pos.phase);
    r.Read("bit_clock_half_phase_den", pos.phase_den);
    started_ = started != 0u;
    clocked_ = clocked != 0u;
    held_    = clocked_ ? TickPosition{} : pos;
    if (!clocked_) return;
    halves_.SetRate(clock_->ClockRate(), rate_.Scaled(2u, 1u));
    halves_.PlaceAt(clock_->Cycles(), pos);
}

REGISTER_SERVICE(Sa11xxSspBitClock);
