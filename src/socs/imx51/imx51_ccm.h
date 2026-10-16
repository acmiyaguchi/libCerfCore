#pragma once

#include "../../execution/guest_cycle_clock.h"
#include <cerfcore/peripherals/peripheral_base.h>
#include "../freescale_timer_clocks.h"

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace cerfcore {

class Imx51ClockInput;
class Imx51CcmDvfs;
class Imx51CcmStatus;
class Imx51CortexA8Platform;

class Imx51Ccm : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return 0x73FD4000u; }
    uint32_t MmioSize() const override { return kSize; }

    uint32_t ReadWord(uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override;

    GuestCycleClock::Rate PerclkRootRate() const;
    GuestCycleClock::Rate IpgClkRate() const;
    GuestCycleClock::Rate SsiClockRate(uint32_t ssi) const;
    GuestCycleClock::Rate UartClockRootRate() const;
    bool                  EcspiClockRootRuns() const;
    bool                  Esdhc2ClockRootRuns() const;
    GuestCycleClock::Rate FpmRate() const;
    uint32_t ClockGate(uint32_t ccgr, uint32_t index) const;
    bool     ClockRunsIn(uint32_t ccgr, uint32_t index, FreescaleLowPowerMode mode) const;
    uint32_t WfiLowPowerMode() const;

    void RegisterRateListener(std::function<void()> fn);
    void RegisterGateListener(std::function<void()> fn);
    void ApplyRates();

private:
    using Rate = GuestCycleClock::Rate;

    static constexpr uint32_t kSize = 0x00001000u;

    uint32_t Reg(uint32_t off) const { return regs_[off >> 2]; }

    void ResetRegisters();
    void NotifyGates();
    void RaiseDividerLoads(uint32_t old, uint32_t now);
    Rate ArmClkRate() const;
    Rate LpApmRate() const;
    Rate BypassClkRate() const;
    Rate StepClkRate() const;
    Rate Pll1SwClkRate() const;
    Rate Pll2SwClkRate() const;
    Rate Pll3SwClkRate() const;
    Rate SwClkSelectRate(uint32_t sel) const;
    Rate PeriphApmClkRate() const;
    Rate PeriphClkRate() const;
    Rate MainBusClkRate() const;
    Rate SsiLpApmRate() const;

    const Imx51ClockInput*             input_    = nullptr;
    const Imx51CortexA8Platform*       platform_ = nullptr;
    Imx51CcmStatus*                    status_   = nullptr;
    Imx51CcmDvfs*                      dvfs_     = nullptr;
    Rate                               applied_{0u, 1u};
    bool                               core_stopped_ = false;
    std::vector<std::function<void()>> listeners_;
    std::vector<std::function<void()>> gate_listeners_;
    std::array<uint32_t, kSize / 4>    regs_{};
};

}
