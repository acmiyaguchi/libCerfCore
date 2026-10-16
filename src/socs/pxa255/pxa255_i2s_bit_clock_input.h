#pragma once

#include <cerfcore/core/service.h>
#include "../../execution/guest_cycle_clock.h"

#include <cstdint>
#include <functional>

namespace cerfcore {

class Pxa255I2sBitClockInput : public Service {
public:
    using Service::Service;

    virtual GuestCycleClock::Rate PadClock() = 0;

    void SetChangeListener(std::function<void(uint64_t at)> fn) { on_change_ = std::move(fn); }

protected:
    void Changed(uint64_t at) { on_change_(at); }

private:
    std::function<void(uint64_t)> on_change_ = [](uint64_t) {};
};

}
