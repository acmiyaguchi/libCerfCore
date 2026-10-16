#pragma once

#include "../../execution/guest_cycle_clock.h"
#include "omap3530_cm_mcbsp_gate_block.h"

#include <cstdint>

class Omap3530CmClockControl;

class Omap3530CmCore : public Omap3530CmMcBspGateBlock {
public:
    using Omap3530CmMcBspGateBlock::Omap3530CmMcBspGateBlock;

    uint32_t MmioBase() const override { return 0x48004A00u; }
    uint32_t MmioSize() const override { return 0x00000100u; }

    void OnReady() override;

    uint32_t ReadWord(uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;
    void     WriteHalf(uint32_t addr, uint16_t value) override;

    GuestCycleClock::Rate L4Iclk() const;

protected:
    const char* Label() const override { return "CM_CORE"; }
    const char* RegisterName(uint32_t off) const override;

    uint32_t McBspBit(uint32_t port) const override;
    uint32_t McBspMask() const override;
    bool     LoadResetLocked() override;

private:
    Omap3530CmClockControl* clock_control_ = nullptr;
};
