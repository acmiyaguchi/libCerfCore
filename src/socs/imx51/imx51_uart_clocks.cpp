#include "../freescale_uart_clocks.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "imx51_ccm.h"
#include "imx51_id.h"

namespace {

/* MCIMX51RM Figure 7-42 (page 7-89): the three UARTs take UART_CLK_ROOT; Table 59-12 RFDIV: "The
   input clock is module_clock". */
class Imx51UartClocks : public FreescaleUartClocks {
public:
    using FreescaleUartClocks::FreescaleUartClocks;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetSocId() == SocId::Imx51;
    }

    void OnReady() override { ccm_ = &emu_.Get<Imx51Ccm>(); }

    GuestCycleClock::Rate ModuleClockRate() const override { return ccm_->UartClockRootRate(); }

    void RegisterRateListener(std::function<void()> fn) override {
        ccm_->RegisterRateListener(std::move(fn));
    }

private:
    Imx51Ccm* ccm_ = nullptr;
};

}

REGISTER_SERVICE_AS(Imx51UartClocks, FreescaleUartClocks);
