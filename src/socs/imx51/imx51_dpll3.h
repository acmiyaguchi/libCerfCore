#pragma once

#include "imx51_dpll.h"

class Imx51Dpll3 final : public Imx51Dpll {
public:
    using Imx51Dpll::Imx51Dpll;
    uint32_t MmioBase() const override { return 0x83F88000u; }

protected:
    uint32_t LockInterruptBit() const override { return 1u << 2; }

    /* MCIMX51RM §7.4.6.6 (page 7-84): with "the on-chip oscillator (24 MHz)" as the source of
       DPLL, "PLL3-initial value = 182 MHz". */
    GuestCycleClock::Rate BootOutputRate() const override {
        return BootReferenceRate().Scaled(91u, 12u);
    }
};
