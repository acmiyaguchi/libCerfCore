#pragma once

#include <cerfcore/core/service.h>
#include <cstdint>

namespace cerfcore {

class MmcCard;

class MmcSlotProvider : public Service {
public:
    using Service::Service;
    virtual MmcCard* CardForSlot(uint32_t slot) = 0;
};

}
