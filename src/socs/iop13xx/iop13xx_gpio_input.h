#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

class Iop13xxGpioInput : public Service {
public:
    using Service::Service;

    virtual uint32_t ReadPins() = 0;
};

}
