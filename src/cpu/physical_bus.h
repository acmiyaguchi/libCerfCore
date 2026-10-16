#pragma once

#include <cerfcore/core/service.h>
#include "../peripherals/peripheral_dispatcher.h"

#include <cstdint>

class PhysicalBus : public Service {
public:
    using Service::Service;

    bool Read (uint32_t pa, MmioWidth width, uint32_t* out);
    bool Write(uint32_t pa, MmioWidth width, uint32_t value);
};
