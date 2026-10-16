#include "../../cpu/arm/arm_core_clock.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "sa1100_id.h"
#include "sa1110_id.h"

namespace cerfcore {

namespace {

class Sa11xxCoreClock : public ArmCoreClock {
public:
    using ArmCoreClock::ArmCoreClock;

    bool ShouldRegister() override {
        const auto soc = emu_.Get<BoardContext>().GetSocId();
        return soc == SocId::Sa1100 || soc == SocId::Sa1110;
    }

    /* SA-1110 Dev Manual §8.2: nRESET clears CCF, selecting the lowest core
       clock, which Table 8-1 gives as 16 x the 3.6864-MHz crystal. */
    GuestCycleClock::Rate ResetRate() const override {
        return GuestCycleClock::Rate{16u * 3686400u, 1u};
    }
};

}

REGISTER_SERVICE_AS(Sa11xxCoreClock, ArmCoreClock);

}
