#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

class Sa11xxBootloaderMemorySetup : public Service {
public:
    using Service::Service;

    virtual uint32_t Mdcnfg() const = 0;
};

}
