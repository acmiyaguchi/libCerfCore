#pragma once

#include "../../execution/guest_cycle_clock.h"
#include "omap3530_prcm_stub_block.h"

#include <cstdint>
#include <functional>
#include <vector>

class Omap3530CmClockControl : public Omap3530PrcmStubBlock {
public:
    using Omap3530PrcmStubBlock::Omap3530PrcmStubBlock;

    uint32_t MmioBase() const override { return 0x48004D00u; }
    uint32_t MmioSize() const override { return 0x00000100u; }

    void OnReady() override;

    uint32_t ReadWord(uint32_t addr) override;
    uint16_t ReadHalf(uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;
    void     WriteHalf(uint32_t addr, uint16_t value) override;

    GuestCycleClock::Rate Dpll4M4X2Input() const;
    GuestCycleClock::Rate CoreClk() const;
    GuestCycleClock::Rate Fclk96M() const;

    void RegisterRateListener(std::function<void()> fn);

protected:
    const char* Label() const override { return "CM_CLOCK_CONTROL"; }
    const char* RegisterName(uint32_t off) const override;

private:
    void LoadResetLocked(bool cold);
    bool DeriveDpll4Locked(GuestCycleClock::Rate& rate, const char*& why) const;
    GuestCycleClock::Rate CoreClkLocked() const;
    GuestCycleClock::Rate Fclk96MLocked() const;
    uint32_t CheckedWriteLocked(uint32_t off, uint32_t value) const;
    void NotifyRate();

    uint64_t osc_hz_ = 0;
    std::vector<std::function<void()>> rate_listeners_;
};
