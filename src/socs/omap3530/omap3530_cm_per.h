#pragma once

#include "omap3530_cm_mcbsp_gate_block.h"

#include <cstdint>

class Omap3530CmPer : public Omap3530CmMcBspGateBlock {
public:
    using Omap3530CmMcBspGateBlock::Omap3530CmMcBspGateBlock;

    uint32_t MmioBase() const override { return 0x48005000u; }
    uint32_t MmioSize() const override { return 0x00000100u; }

protected:
    const char* Label() const override { return "CM_PER"; }
    const char* RegisterName(uint32_t off) const override;

    uint32_t McBspBit(uint32_t port) const override;
    uint32_t McBspMask() const override;
    bool     LoadResetLocked() override;
};
