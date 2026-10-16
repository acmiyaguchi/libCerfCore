#include "sa11xx_test_unit.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "../../execution/guest_cycle_clock.h"
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include "sa1100_id.h"
#include "sa1110_id.h"
#include "sa11xx_gpio.h"

namespace cerfcore {

namespace {

/* SA-1110 Developer's Manual App. D.1: TUCR bit 10 MR. SA-1100 TRM §10.8 (printed 10-31): "Test
   unit control register (TUCR) to set bit 10." */
constexpr uint32_t kMr = 1u << 10;

constexpr uint32_t kGp21 = 21u;
constexpr uint32_t kGp22 = 22u;
constexpr uint32_t kGp27 = 27u;

constexpr uint32_t kTselShift     = 29u;
constexpr uint32_t kTsel32k       = 0u;
constexpr uint32_t kTsel3686k     = 1u;
constexpr uint32_t kTselVddRing   = 2u;
constexpr uint32_t kTsel96MhzPll  = 3u;
constexpr uint32_t kTsel32kRclk   = 4u;
constexpr uint32_t kTsel3686kB    = 5u;
constexpr uint32_t kTselMainPll   = 6u;

constexpr uint64_t kOscillatorHz    = 3686400u;
constexpr uint64_t kSecondaryPllMul = 13u;

}

bool Sa11xxTestUnit::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && (bd->GetSocId() == SocId::Sa1110 || bd->GetSocId() == SocId::Sa1100);
}

/* SA-1110 Developer's Manual App. D.1: every TUCR bit resets to 0; §9.6.1.1 RSRR: "Writing a one
   to this bit causes all on-chip resources to reset"; §9.6: "Sleep reset does not affect the
   power manager, RTC, or GPIO wake-up register". SA-1100 TRM §9.6.1.1 carries the same RSRR text. */
void Sa11xxTestUnit::OnReady() {
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) { tucr_ = 0u; });
    emu_.Get<Sa11xxGpio>().RegisterPinConfigListener([this] { NotifyChange(); });
    emu_.Get<GuestCycleClock>().RegisterRateListener([this] {
        if ((tucr_ >> kTselShift) == kTselMainPll) NotifyChange();
    });
}

void Sa11xxTestUnit::Write(uint32_t value) {
    tucr_ = value;
    NotifyChange();
}

/* App. D.1 MR: "1 - GP 21 and GP 22 are reserved for use as MBGNT and MBREQ, respectively",
   "0 - GP 21 and GP 22 are not used for an alternate function"; §10.8: GPDR bit 21 set and bit 22
   clear, GAFR bits 21 and 22 set, TUCR bit 10 set. §9.1.2: GP 21 alternate TIC_ACK/MBGNT. */
Sa11xxTestUnit::Mbgnt Sa11xxTestUnit::MbgntPin() const {
    const auto& gpio = emu_.Get<Sa11xxGpio>();
    const Sa11xxGpio::PinConfig gp21 = gpio.Pin(kGp21);
    if ((tucr_ & kMr) != 0u) {
        const Sa11xxGpio::PinConfig gp22 = gpio.Pin(kGp22);
        const bool configured = gp21.alternate && gp21.output && gp22.alternate && !gp22.output;
        return configured ? Mbgnt::Arbiter : Mbgnt::Undetermined;
    }
    if (gp21.alternate || !gp21.output) return Mbgnt::Undetermined;
    return gp21.latch ? Mbgnt::High : Mbgnt::Low;
}

/* App. D.1 TSEL: "To observe these clocks, set bit 27 to one in the GAFR and GPDR registers";
   §8.1: the 32.768-kHz and 3.6864-MHz crystals; §8: "The primary PLL provides clocks for the
   core logic". */
bool Sa11xxTestUnit::Gp27ClockRate(GuestCycleClock::Rate& rate) const {
    const Sa11xxGpio::PinConfig gp27 = emu_.Get<Sa11xxGpio>().Pin(kGp27);
    if (!gp27.alternate || !gp27.output) return false;
    switch (tucr_ >> kTselShift) {
        case kTsel32k:
        case kTsel32kRclk:
            rate = {32768u, 1u};
            return true;
        case kTselMainPll:
            rate = emu_.Get<GuestCycleClock>().ClockRate();
            rate.den *= 16u;
            return true;
        /* §11.12.1 (printed 11-127): "multiplying the 3.6864-MHz on-chip oscillator by 13";
           App. D.1 TSEL 011 "96-MHz PLL/4". */
        case kTsel96MhzPll:
            rate = {kOscillatorHz * kSecondaryPllMul * 2u, 4u};
            return true;
        case kTsel3686k:
        case kTsel3686kB:
            rate = {kOscillatorHz, 1u};
            return true;
        case kTselVddRing:
        default:
            rate = {kOscillatorHz, 1u};
            return true;
    }
}

void Sa11xxTestUnit::RegisterChangeListener(std::function<void()> fn) {
    listeners_.push_back(std::move(fn));
}

void Sa11xxTestUnit::NotifyChange() {
    for (auto& fn : listeners_) fn();
}

void Sa11xxTestUnit::Save(StateWriter& w) const {
    w.Write("tucr", tucr_);
}

void Sa11xxTestUnit::Restore(StateReader& r) {
    r.Read("tucr", tucr_);
}

REGISTER_SERVICE(Sa11xxTestUnit);

}
