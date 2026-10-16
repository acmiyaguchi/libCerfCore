#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

class SymbolHamptonsDisplay : public Service {
public:
    using Service::Service;

    virtual uint32_t DisplayId() const = 0;
};

}
