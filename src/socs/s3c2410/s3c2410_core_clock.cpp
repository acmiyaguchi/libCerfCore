#include "../../cpu/arm/arm_core_clock.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "s3c2410_clocks.h"
#include "s3c2410_id.h"

namespace cerfcore {

namespace {

class S3c2410CoreClock : public ArmCoreClock {
public:
    using ArmCoreClock::ArmCoreClock;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetSocId() == SocId::S3c2410;
    }

    GuestCycleClock::Rate ResetRate() const override {
        return GuestCycleClock::Rate{emu_.Get<S3C2410Clocks>().CoreClockHz(), 1u};
    }
};

}

REGISTER_SERVICE_AS(S3c2410CoreClock, ArmCoreClock);

}
