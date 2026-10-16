#include "../../cpu/arm/arm_core_clock.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "imx31_id.h"

namespace cerfcore {

namespace {

class Imx31CoreClock : public ArmCoreClock {
public:
    using ArmCoreClock::ArmCoreClock;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetSocId() == SocId::Imx31;
    }

    /* MCIMX31RM §3.5.3 arm_clk max = 532 MHz. */
    GuestCycleClock::Rate ResetRate() const override {
        return GuestCycleClock::Rate{532000000u, 1u};
    }
};

}

REGISTER_SERVICE_AS(Imx31CoreClock, ArmCoreClock);

}
