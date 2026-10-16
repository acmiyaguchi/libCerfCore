#pragma once

#include <cerfcore/core/service.h>
#include "../../execution/guest_cycle_clock.h"

namespace cerfcore {

class ArmCoreClock : public Service {
public:
    using Service::Service;

    virtual GuestCycleClock::Rate ResetRate() const = 0;
};

}
