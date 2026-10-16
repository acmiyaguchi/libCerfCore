#pragma once

#include "omap3530_prcm_stub_block.h"

#include <cstdint>
#include <functional>
#include <vector>

class Omap3530CmMcBspGateBlock : public Omap3530PrcmStubBlock {
public:
    using Omap3530PrcmStubBlock::Omap3530PrcmStubBlock;

    void OnReady() override;
    void WriteWord(uint32_t addr, uint32_t value) override;
    void WriteHalf(uint32_t addr, uint16_t value) override;

    bool McBspFclkEnabled(uint32_t port) const;
    bool McBspIclkEnabled(uint32_t port) const;
    void RegisterClockListener(std::function<void()> fn);

protected:
    virtual uint32_t McBspBit(uint32_t port) const = 0;
    virtual uint32_t McBspMask() const             = 0;
    virtual bool     LoadResetLocked()             = 0;

    void NotifyClock();

private:
    uint64_t McBspGates() const;
    uint64_t McBspGatesLocked() const;

    std::vector<std::function<void()>> clock_listeners_;
    bool                               reset_changed_ = false;
};
