#pragma once

#include <cstdint>

#include <cerfcore/core/service.h>

class Sa11xxPreKernelPpcr : public Service {
public:
    using Service::Service;

    virtual uint32_t PpcrValue() const = 0;
};
