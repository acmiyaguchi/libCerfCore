#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/boards/board_context.h>
#include "msm8255_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include "../../peripherals/peripheral_dispatcher.h"

#include <cstdint>

namespace {

/* Linux arch/arm/mach-msm msm_iomap-7x30.h: MSM_CLK_CTL_PHYS, MSM_CLK_CTL_SIZE. */
constexpr uint32_t kClkCtlBase = 0xAB800000u;
constexpr uint32_t kClkCtlSize = 0x00001000u;

/* Linux arch/arm/mach-msm acpuclock-7x30.c: PLL2_L_VAL_ADDR MSM_CLK_CTL_BASE+0x33C. */
constexpr uint32_t kRegPll2Mode = 0x338u;

/* Linux arch/arm/mach-msm clock-7x30-vendor.c: PLL2_STATUS_BASE_REG REG_BASE(0x0350),
   pll2_clk status_mask BIT(16). */
constexpr uint32_t kRegPll2Status    = 0x350u;
constexpr uint32_t kPll2StatusActive = 0x00010000u;

/* Linux drivers/clk/qcom/common.h: PLL_VOTE_FSM_ENA BIT(20). */
constexpr uint32_t kPll2ModeFsmEnabled = 0x00100000u;

class Msm8255ClkCtl : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetSocId() == SocId::Msm8255;
    }

    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return kClkCtlBase; }
    uint32_t MmioSize() const override { return kClkCtlSize; }

    uint32_t ReadWord(uint32_t addr) override {
        switch (addr - MmioBase()) {
        case kRegPll2Mode:   return kPll2ModeFsmEnabled;
        case kRegPll2Status: return kPll2StatusActive;
        default:             HaltUnsupportedAccess("ReadWord", addr, 0);
        }
    }
};

}

REGISTER_SERVICE(Msm8255ClkCtl);
