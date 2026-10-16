#pragma once

#include <cerfcore/core/service.h>

namespace cerfcore {

class Pd6710CardIrqLine : public Service {
public:
    using Service::Service;
    ~Pd6710CardIrqLine() override = default;

    virtual void Assert()   = 0;
    virtual void Deassert() = 0;
};

}
