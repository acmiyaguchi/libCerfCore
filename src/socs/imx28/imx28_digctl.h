#pragma once

#include <cerfcore/state/state_stream.h>
#include "../oscillator_ticks.h"
#include "imx28_apb_peripheral.h"
#include "imx28_sct_reg.h"

#include <cstdint>

namespace cerfcore {

/* MCIMX28RM Rev. 2 chapter 19 (DIGCTL) at 0x8001_C000. */
class Imx28Digctl : public Imx28ApbPeripheral {
public:
    using Imx28ApbPeripheral::Imx28ApbPeripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return 0x8001C000u; }
    uint32_t MmioSize() const override { return 0x00002000u; }

    uint32_t ReadWord(uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;
    void     SaveState(StateWriter& w) override;
    void     RestoreState(StateReader& r) override;

    /* Section 19.4.1 CTRL USB0_CLKGATE [2], USB1_CLKGATE [16]. */
    bool UsbClockGated(uint32_t unit) const { return ((ctrl_ >> (unit == 0u ? 2u : 16u)) & 1u) != 0u; }

private:
    void     LoadResetValues();
    void     WriteCtrl(Imx28SctOp op, uint32_t value, uint32_t addr);
    void     RequireReset(Imx28SctOp op, uint32_t reset, uint32_t writable, uint32_t value,
                          uint32_t addr);
    uint32_t Microseconds();

    StoppableOscillatorTicks us_{emu_, false};

    uint32_t ctrl_       = 0u;
    uint32_t write_once_ = 0u;
    uint32_t scratch_[2] = {};
    uint32_t us_base_    = 0u;
    bool     written_    = false;
};

}
