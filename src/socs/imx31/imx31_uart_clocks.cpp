#include "../freescale_uart_clocks.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "imx31_ccm.h"
#include "imx31_id.h"

namespace {

/* MCIMX31RM Table 31-11 RFDIV: "The input clock is ipg_perclk"; section 31.1 (page 31-3) the BRM clock
   ipg_perclk allows "frequency scaling on main clock (ipg_clk)"; section 3.4.4.3.2 (page 3-44)
   ipg_per_baud "is the clock for the peripherals that will not be affected by the DVFS changes". */
class Imx31UartClocks : public FreescaleUartClocks {
public:
    using FreescaleUartClocks::FreescaleUartClocks;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetSocId() == SocId::Imx31;
    }

    void OnReady() override { ccm_ = &emu_.Get<Imx31Ccm>(); }

    GuestCycleClock::Rate ModuleClockRate() const override {
        return GuestCycleClock::Rate{ccm_->PerClkHz(), 1u};
    }

    void RegisterRateListener(std::function<void()> fn) override {
        ccm_->RegisterRateListener(std::move(fn));
    }

private:
    Imx31Ccm* ccm_ = nullptr;
};

}

REGISTER_SERVICE_AS(Imx31UartClocks, FreescaleUartClocks);
