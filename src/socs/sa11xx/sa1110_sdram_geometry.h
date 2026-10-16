#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

class Sa1110SdramGeometry : public Service {
public:
    using Service::Service;

    virtual uint32_t RowBits() const = 0;
    virtual uint32_t ColumnBits() const = 0;
    virtual uint32_t BankBits() const = 0;
};
