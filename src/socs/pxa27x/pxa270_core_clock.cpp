#include "../../cpu/arm/arm_core_clock.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "pxa270_id.h"

namespace {

class Pxa270CoreClock : public ArmCoreClock {
public:
    using ArmCoreClock::ArmCoreClock;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetSocId() == SocId::Pxa270;
    }

    /* Intel PXA27x Developer's Manual 280000-001 Table 3-7 (page 3-20): "L = 7
       (Core = 91.0 MHz) is used for hardware boot-up frequency". */
    GuestCycleClock::Rate ResetRate() const override {
        return GuestCycleClock::Rate{91000000u, 1u};
    }
};

}

REGISTER_SERVICE_AS(Pxa270CoreClock, ArmCoreClock);
