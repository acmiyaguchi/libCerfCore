#include "../../cpu/arm/arm_core_clock.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "omap3530_cm_mpu.h"
#include "omap3530_id.h"

namespace {

class Omap3530CoreClock : public ArmCoreClock {
public:
    using ArmCoreClock::ArmCoreClock;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetSocId() == SocId::Omap3530;
    }

    GuestCycleClock::Rate ResetRate() const override {
        return GuestCycleClock::Rate{emu_.Get<Omap3530CmMpu>().ArmFclkHz(), 1u};
    }
};

}

REGISTER_SERVICE_AS(Omap3530CoreClock, ArmCoreClock);
