#pragma once

#include "imx51_dpll.h"

class Imx51Dpll1 final : public Imx51Dpll {
public:
    using Imx51Dpll::Imx51Dpll;
    uint32_t MmioBase() const override { return 0x83F80000u; }

protected:
    uint32_t LockInterruptBit() const override { return 1u << 0; }

    /* Table 9-2 BT_LPB "00 LPBM disabled", BT_LPB_FREQ "111 Normal boot frequency (400 MHz)";
       §54.2.2.2: SBMR "contains bits that reflect the status of Boot Mode Pins", BT_LPB [24:23]. */
    GuestCycleClock::Rate BootOutputRate() const override {
        return GuestCycleClock::Rate{400000000u, 1u};
    }
};
