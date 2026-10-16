#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

class Imx51ClockInput : public Service {
public:
    using Service::Service;

    virtual uint64_t OscHz() const = 0;
    virtual uint64_t CkilHz() const = 0;
    virtual uint64_t CkihHz() const = 0;
    virtual uint64_t Ckih2Hz() const = 0;
};
