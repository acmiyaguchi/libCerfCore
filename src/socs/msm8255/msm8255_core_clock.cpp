#include "../../cpu/arm/arm_core_clock.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "msm8255_id.h"

namespace {

class Msm8255CoreClock : public ArmCoreClock {
public:
    using ArmCoreClock::ArmCoreClock;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetSocId() == SocId::Msm8255;
    }

    GuestCycleClock::Rate ResetRate() const override {
        return GuestCycleClock::Rate{1401600u * 1000u, 1u};
    }
};

}

REGISTER_SERVICE_AS(Msm8255CoreClock, ArmCoreClock);
