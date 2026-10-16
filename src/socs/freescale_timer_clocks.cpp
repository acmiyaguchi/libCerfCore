#include "freescale_timer_clocks.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>

GuestCycleClock::Rate FreescaleTimerClocks::PrescaledRate(FreescaleTimerUnit unit, uint32_t base,
                                                          GuestCycleClock::Rate input,
                                                          uint32_t prescaler) const {
    const uint64_t divide = uint64_t{prescaler} + 1u;
    if (!input.ScaleFits(1u, divide)) {
        emu_.Get<Fatal>().Die("%s %08X: the %llu/%llu Hz input divided by %llu overflows the "
                              "64-bit rate", unit == FreescaleTimerUnit::kGpt ? "GPT" : "EPIT",
                              base, static_cast<unsigned long long>(input.num),
                              static_cast<unsigned long long>(input.den),
                              static_cast<unsigned long long>(divide));
    }
    return input.Scaled(1u, divide);
}
