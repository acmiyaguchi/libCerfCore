#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

class StateWriter;
class StateReader;

class SpiSlave : public Service {
public:
    using Service::Service;
    ~SpiSlave() override = default;

    virtual void SaveState(StateWriter&) {}
    virtual void RestoreState(StateReader&) {}

    virtual uint8_t Exchange(uint8_t mosi) = 0;
};
