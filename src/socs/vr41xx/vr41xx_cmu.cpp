#include "vr41xx_cmu.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>

namespace cerfcore {

void Vr41xxCmu::RequireTclock(uint16_t mask_bit, const char* operation) const {
    const uint16_t mask = ClockMask();
    if ((mask & mask_bit) == 0u) {
        emu_.Get<Fatal>().Die("Vr41xxCmu: %s with CMUCLKMSK 0x%04X masking its TClock; the unit "
                              "without TClock is not modeled", operation, mask);
    }
}

void Vr41xxCmu::RegisterClockListener(uint16_t mask_bit, std::function<void(bool)> fn) {
    clock_listeners_.push_back(ClockListener{mask_bit, std::move(fn)});
}

void Vr41xxCmu::ClockMaskWriting(uint16_t next) const {
    const uint16_t changed = static_cast<uint16_t>(ClockMask() ^ next);
    for (const ClockListener& listener : clock_listeners_) {
        if ((changed & listener.mask_bit) != 0u) listener.fn((next & listener.mask_bit) != 0u);
    }
}

}
