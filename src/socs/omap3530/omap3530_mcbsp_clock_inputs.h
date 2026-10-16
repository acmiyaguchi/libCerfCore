#pragma once

#include <cerfcore/core/service.h>
#include "../../execution/guest_cycle_clock.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace cerfcore {

class Omap3530CmClockControl;
class Omap3530CmCore;
class Omap3530CmMcBspGateBlock;
class Omap3530CmPer;
class Omap3530McBspClockPad;
class Omap3530SyscPadconf;

class Omap3530McBspClockInputs : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    GuestCycleClock::Rate Iclk(uint32_t port) const;
    GuestCycleClock::Rate Clks(uint32_t port) const;
    bool                  ClkrFromClkx(uint32_t port) const;
    bool                  FsrFromFsx(uint32_t port) const;

    void RegisterListener(std::function<void()> fn);

private:
    const Omap3530CmMcBspGateBlock& GateBlock(uint32_t port) const;
    bool                            FclkEnabled(uint32_t port) const;
    void                            Notify();

    Omap3530CmClockControl*            clock_control_ = nullptr;
    Omap3530CmCore*                    core_          = nullptr;
    Omap3530CmPer*                     per_           = nullptr;
    Omap3530SyscPadconf*               padconf_       = nullptr;
    Omap3530McBspClockPad*             pad_           = nullptr;
    std::vector<std::function<void()>> listeners_;
};

}
