#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

class Wm9713MasterClocks : public Service {
public:
    using Service::Service;

    virtual uint64_t MclkaHz() const = 0;
    virtual uint64_t MclkbHz() const = 0;
};

}
