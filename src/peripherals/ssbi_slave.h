#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

class StateWriter;
class StateReader;

class SsbiSlave : public Service {
public:
    using Service::Service;

    virtual uint8_t ReadReg(uint16_t reg) = 0;
    virtual void    WriteReg(uint16_t reg, uint8_t value) = 0;

    virtual void SaveState(StateWriter&) {}
    virtual void RestoreState(StateReader&) {}
    virtual void PostRestore() {}
};
