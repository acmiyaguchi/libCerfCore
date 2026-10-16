#include "../freescale_timer_clocks.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "imx51_ccm.h"
#include "imx51_clock_input.h"
#include "imx51_id.h"

#include <cstdint>

namespace cerfcore {

namespace {

/* MCIMX51RM Table 7-35 CCGR2: CG1 epit1_ipg_clk, CG2 epit1_highfreq, CG3
   epit2_ipg_clk, CG4 epit2_highfreq, CG9 gpt_ipg_clk, CG10 gpt_highfreq. */
constexpr uint32_t kCcgr2 = 2u;

class Imx51TimerClocks : public FreescaleTimerClocks {
public:
    using FreescaleTimerClocks::FreescaleTimerClocks;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetSocId() == SocId::Imx51;
    }

    void OnReady() override { ccm_ = &emu_.Get<Imx51Ccm>(); }

    /* MCIMX51RM Table 7-41: epit1_highfreq, epit2_highfreq and gpt_highfreq are
       PERCLK-dependent. §29.4.2 (page 29-10): ipg_clk_32k "is the 32Khz low reference clock
       which is provided by the CCM"; Table 7-35 gates no 32 kHz clock. */
    GuestCycleClock::Rate InputRate(FreescaleTimerUnit unit,
                                    FreescaleTimerInput input) const override {
        if (!InputRunsIn(unit, input, FreescaleLowPowerMode::kRun)) return GuestCycleClock::Rate{0u, 1u};
        if (input == FreescaleTimerInput::kLowfreq) {
            return GuestCycleClock::Rate{emu_.Get<Imx51ClockInput>().CkilHz(), 1u};
        }
        if (input == FreescaleTimerInput::kHighfreq) return ccm_->PerclkRootRate();
        return ccm_->IpgClkRate();
    }

    /* MCIMX51RM §29.4.2 (page 29-10): ipg_clk_32k "is supposed to be on in low power mode when
       ipg_clk is turned off". */
    bool InputRunsIn(FreescaleTimerUnit unit, FreescaleTimerInput input,
                     FreescaleLowPowerMode mode) const override {
        if (input == FreescaleTimerInput::kLowfreq) return true;
        return ccm_->ClockRunsIn(kCcgr2, GateIndex(unit, input), mode);
    }

    /* MCIMX51RM Table 7-26 CLPCR LPM [1:0]: 00 remain in run mode, 01 wait mode, 10 STOP mode,
       11 LPSR mode; §7.4.8.4 (page 7-117): "LPSR is similar to STOP mode". */
    FreescaleLowPowerMode WfiMode() const override {
        switch (ccm_->WfiLowPowerMode()) {
            case 0u:  return FreescaleLowPowerMode::kRun;
            case 1u:  return FreescaleLowPowerMode::kWait;
            default:  return FreescaleLowPowerMode::kStop;
        }
    }

    void RegisterRateListener(std::function<void()> fn) override {
        ccm_->RegisterRateListener(std::move(fn));
    }

private:
    static uint32_t GateIndex(FreescaleTimerUnit unit, FreescaleTimerInput input) {
        const bool hf = input == FreescaleTimerInput::kHighfreq;
        switch (unit) {
            case FreescaleTimerUnit::kEpit1: return hf ? 2u : 1u;
            case FreescaleTimerUnit::kEpit2: return hf ? 4u : 3u;
            default:                         return hf ? 10u : 9u;
        }
    }

    Imx51Ccm* ccm_ = nullptr;
};

}

REGISTER_SERVICE_AS(Imx51TimerClocks, FreescaleTimerClocks);

}
