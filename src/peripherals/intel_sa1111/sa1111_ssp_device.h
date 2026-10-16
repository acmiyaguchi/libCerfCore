#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

class StateReader;
class StateWriter;

class Sa1111SspDevice : public Service {
public:
    using Service::Service;

    virtual uint32_t Shift(uint32_t out, uint32_t bits) = 0;
    virtual void     EndTransaction() = 0;
    virtual void     SaveState(StateWriter& w) = 0;
    virtual void     RestoreState(StateReader& r) = 0;
};
