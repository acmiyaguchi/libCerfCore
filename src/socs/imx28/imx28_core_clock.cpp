#include "../../cpu/arm/arm_core_clock.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "imx28_id.h"

namespace cerfcore {

namespace {

class Imx28CoreClock : public ArmCoreClock {
public:
    using ArmCoreClock::ArmCoreClock;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetSocId() == SocId::Imx28;
    }

    /* MCIMX28RM Rev. 2 10.8.26 CLKSEQ reset BYPASS_CPU = 1, 10.8.6 CPU
       reset DIV_XTAL = 1, Table 10-1 clk_xtal24m 24 MHz. */
    GuestCycleClock::Rate ResetRate() const override {
        return GuestCycleClock::Rate{24000000u, 1u};
    }
};

}

REGISTER_SERVICE_AS(Imx28CoreClock, ArmCoreClock);

}
