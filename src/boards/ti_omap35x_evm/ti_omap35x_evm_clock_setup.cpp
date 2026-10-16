#include "../../socs/omap3530/omap3530_board_clock_setup.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "ti_omap35x_evm_family_id.h"

#include <cstdint>

namespace {

constexpr uint64_t kOscSysClkHz = 26000000ull;

constexpr uint32_t kMpuDpllFreqSel = 7u;
constexpr uint32_t kMpuDpllLock    = 7u;
constexpr uint32_t kMpuClkSrc      = 1u;
constexpr uint32_t kMpuDpllMult    = 300u;
constexpr uint32_t kMpuDpllDiv     = 12u;
constexpr uint32_t kMpuDpllM2      = 1u;

constexpr uint32_t kPeriphDpllFreqSel = 7u;
constexpr uint32_t kPeriphDpllLock    = 7u;
constexpr uint32_t kPeriphDpllMult    = 216u;
constexpr uint32_t kPeriphDpllDiv     = 12u;

/* U-Boot OMAP3 prcm_init: CORE_N_13 0x0C, CORE_M2_13 1, CORE_L3_DIV 2 "166MHz", CORE_L4_DIV 2, PER_M2_13 9, SOURCE_96M
   cleared; SPRUF98Y Table 4-456 (page 596) SYSCLKDIV reset 0x1, section 4.7.3.3 (page 307) CLKOUTX2 = (Fref x 2 x M)/(N+1),
   Table 4-178 (page 468) FREQSEL 0x7 = 1.75-2.1 MHz, Table 4-145 (page 455) [11:8] reset 0x1. */
constexpr uint32_t kCoreDpllFreqSel = 7u;
constexpr uint32_t kCoreDpllLock    = 7u;
constexpr uint32_t kCoreDpllMult    = 166u;
constexpr uint32_t kCoreDpllDiv     = 12u;
constexpr uint32_t kCoreDpllM2      = 1u;
constexpr uint32_t kDiv96M          = 9u;
constexpr uint32_t kClkselCoreRsvd  = 1u;
constexpr uint32_t kClkselL4Div2    = 2u;
constexpr uint32_t kClkselL3Div2    = 2u;

class TiOmap35xEvmClockSetup : public Omap3530BoardClockSetup {
public:
    using Omap3530BoardClockSetup::Omap3530BoardClockSetup;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::TiOmap35xEvm;
    }

    uint64_t OscSysClkHz() const override { return kOscSysClkHz; }

    /* TI SWCU056C Figure 2 (printed p. 9): TPS65950 HFCLKOUT drives OMAP3530
       sys_xtalin, sys_xtalout unconnected; §3.1.1 (printed p. 8): the TPS65950
       "delivers a square digital waveform to the entire applicative system". */
    bool SysXtalinIsSquareClock() const override { return true; }

    Omap3530MpuDpllSetting BootMpuDpll() const override {
        return { (kMpuDpllFreqSel << 4) | kMpuDpllLock,
                 (kMpuClkSrc << 19) | (kMpuDpllMult << 8) | kMpuDpllDiv,
                 kMpuDpllM2 };
    }

    Omap3530PeriphDpllSetting BootPeriphDpll() const override {
        return { (kPeriphDpllFreqSel << 20) | (kPeriphDpllLock << 16),
                 (kPeriphDpllMult << 8) | kPeriphDpllDiv };
    }

    Omap3530CoreClockSetting BootCoreClocks() const override {
        return { (kCoreDpllFreqSel << 4) | kCoreDpllLock,
                 (kCoreDpllM2 << 27) | (kCoreDpllMult << 16) | (kCoreDpllDiv << 8),
                 kDiv96M,
                 (kClkselCoreRsvd << 8) | (kClkselL4Div2 << 2) | kClkselL3Div2 };
    }

    bool BootEnablesGpt1Clocks() const override { return true; }
};

}

REGISTER_SERVICE_AS(TiOmap35xEvmClockSetup, Omap3530BoardClockSetup);
