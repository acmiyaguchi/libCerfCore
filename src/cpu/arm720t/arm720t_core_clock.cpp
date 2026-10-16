#include "../arm/arm_core_clock.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "arm720t_id.h"

namespace {

class Arm720tCoreClock : public ArmCoreClock {
public:
    using ArmCoreClock::ArmCoreClock;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetSocId() == SocId::Arm720t;
    }

    GuestCycleClock::Rate ResetRate() const override {
        return GuestCycleClock::Rate{70000000u, 1u};
    }
};

}

REGISTER_SERVICE_AS(Arm720tCoreClock, ArmCoreClock);
