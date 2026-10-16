#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/boards/board_context.h>
#include "msm8255_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include "../../peripherals/peripheral_dispatcher.h"

#include <cstdint>

namespace {

/* Linux arch/arm/mach-msm msm_iomap-7x30.h: MSM_ACC_PHYS, MSM_ACC_SIZE. */
constexpr uint32_t kAccBase = 0xC0101000u;
constexpr uint32_t kAccSize = 0x00001000u;

/* Linux arch/arm/mach-msm acpuclock-7x30.c: SCSS_CLK_CTL_ADDR is
   MSM_ACC_BASE + 0x04 and SCSS_CLK_SEL_ADDR is MSM_ACC_BASE + 0x08. */
constexpr uint32_t kRegClkCtl = 0x04u;
constexpr uint32_t kRegClkSel = 0x08u;

/* Linux arch/arm/mach-msm acpuclock-7x30.c: acpuclk_init decodes
   acpu_src_sel and acpu_src_div from SCSS_CLK_CTL banked by SCSS_CLK_SEL,
   and acpu_freq_tbl pairs acpu_clk_khz 1401600 with sel 3, div 0. */
constexpr uint32_t kClkCtl = 0x00003000u;
constexpr uint32_t kClkSel = 0x00000000u;

class Msm8255Acc : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetSocId() == SocId::Msm8255;
    }

    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return kAccBase; }
    uint32_t MmioSize() const override { return kAccSize; }

    uint32_t ReadWord(uint32_t addr) override {
        switch (addr - MmioBase()) {
        case kRegClkCtl: return kClkCtl;
        case kRegClkSel: return kClkSel;
        default:         HaltUnsupportedAccess("ReadWord", addr, 0);
        }
    }
};

}

REGISTER_SERVICE(Msm8255Acc);
