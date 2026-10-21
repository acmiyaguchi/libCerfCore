#pragma once

#include "../core/service.h"

#include <cstdint>

namespace cerfcore {

class HostDisplays : public Service {
public:
    using Service::Service;

    virtual void LargestDisplaySize(uint32_t& width, uint32_t& height) = 0;
    virtual int  HighestRefreshRate() = 0;
};

}
