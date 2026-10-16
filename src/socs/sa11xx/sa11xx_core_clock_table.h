#pragma once

#include <cstdint>

#include <cerfcore/core/service.h>

namespace cerfcore {

class Sa11xxCoreClockTable : public Service {
public:
    using Service::Service;

    virtual uint32_t MaxCcf() const = 0;
};

}
