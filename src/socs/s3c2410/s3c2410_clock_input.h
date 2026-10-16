#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

class S3C2410ClockInput : public Service {
public:
    using Service::Service;

    virtual uint64_t FinHz() const = 0;
};

}
