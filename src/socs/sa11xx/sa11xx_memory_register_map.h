#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

class Sa11xxMemoryRegisterMap : public Service {
public:
    using Service::Service;

    static constexpr uint32_t kMaxRegisters = 13;

    virtual uint32_t RegisterCount() const = 0;
    virtual void ApplyResetValues(uint32_t (&regs)[kMaxRegisters]) const = 0;
};
