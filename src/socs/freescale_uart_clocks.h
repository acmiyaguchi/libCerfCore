#pragma once

#include <cerfcore/core/service.h>
#include "../execution/guest_cycle_clock.h"

#include <functional>

namespace cerfcore {

class FreescaleUartClocks : public Service {
public:
    using Service::Service;

    virtual GuestCycleClock::Rate ModuleClockRate() const = 0;

    virtual void RegisterRateListener(std::function<void()> fn) = 0;
};

}
