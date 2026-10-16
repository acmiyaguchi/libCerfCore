#include "../../cpu/arm/arm_core_clock.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "imx51_id.h"

namespace {

class Imx51CoreClock : public ArmCoreClock {
public:
    using ArmCoreClock::ArmCoreClock;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetSocId() == SocId::Imx51;
    }

    GuestCycleClock::Rate ResetRate() const override {
        return GuestCycleClock::Rate{600000000u, 1u};
    }
};

}

REGISTER_SERVICE_AS(Imx51CoreClock, ArmCoreClock);
