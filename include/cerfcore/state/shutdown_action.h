#pragma once

#include "../core/service.h"
#include "shutdown_choice.h"

namespace cerfcore {

class ShutdownAction : public Service {
public:
    using Service::Service;

    void Perform(ShutdownChoice c);
};

}
