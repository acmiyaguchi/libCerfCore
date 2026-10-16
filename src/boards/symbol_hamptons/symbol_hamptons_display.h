#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

class SymbolHamptonsDisplay : public Service {
public:
    using Service::Service;

    virtual uint32_t DisplayId() const = 0;
};
