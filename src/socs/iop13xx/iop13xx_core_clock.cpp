#include "../../cpu/arm/arm_core_clock.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "iop13xx_id.h"

namespace {

class Iop13xxCoreClock : public ArmCoreClock {
public:
    using ArmCoreClock::ArmCoreClock;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetSocId() == SocId::Iop13xx;
    }

    /* Linux v3.18 arch/arm/mach-iop13xx/include/mach/time.h decodes the
       IOP13xx CORE_FREQ_800 strap as 800000000 Hz. */
    GuestCycleClock::Rate ResetRate() const override {
        return GuestCycleClock::Rate{800000000u, 1u};
    }
};

}

REGISTER_SERVICE_AS(Iop13xxCoreClock, ArmCoreClock);
