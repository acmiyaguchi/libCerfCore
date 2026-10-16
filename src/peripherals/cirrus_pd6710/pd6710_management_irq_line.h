#pragma once

#include <cerfcore/core/service.h>

class Pd6710ManagementIrqLine : public Service {
public:
    using Service::Service;
    ~Pd6710ManagementIrqLine() override = default;

    virtual void Pulse() = 0;
};
