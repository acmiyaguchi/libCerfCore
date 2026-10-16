#include "msm8255_clock_reset.h"

#include <cerfcore/boards/board_context.h>
#include "msm8255_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>

REGISTER_SERVICE(Msm8255ClockReset);

bool Msm8255ClockReset::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Msm8255;
}

void Msm8255ClockReset::RegisterListener(uint32_t clock,
                                         std::function<void()> fn) {
    listeners_.push_back({clock, std::move(fn)});
}

void Msm8255ClockReset::Reset(uint32_t clock) {
    bool served = false;
    for (const Listener& listener : listeners_) {
        if (listener.clock == clock) {
            listener.fn();
            served = true;
        }
    }
    if (!served) {
        emu_.Get<Fatal>().Die(
            "msm8255 clock reset: clock %u drives no block this program "
            "resets", clock);
    }
}
