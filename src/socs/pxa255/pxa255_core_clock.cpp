#include "../../cpu/arm/arm_core_clock.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "pxa255_id.h"

namespace cerfcore {

namespace {

class Pxa255CoreClock : public ArmCoreClock {
public:
    using ArmCoreClock::ArmCoreClock;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetSocId() == SocId::Pxa255;
    }

    /* PXA255 manual Table 3-20: CCCR reset 0x121, L = 27, M = 1; Table 3-1
       row "27 1" run frequency 99.5 MHz = 27 x 3.6864 MHz. */
    GuestCycleClock::Rate ResetRate() const override {
        return GuestCycleClock::Rate{99532800u, 1u};
    }
};

}

REGISTER_SERVICE_AS(Pxa255CoreClock, ArmCoreClock);

}
