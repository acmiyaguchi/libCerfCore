#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

class StateWriter;
class StateReader;

class Sa11xxSspDevice : public Service {
public:
    using Service::Service;

    enum class Pins { SerialPort4, Gpio10To13 };

    virtual Pins     WiredTo() const = 0;
    virtual uint16_t Exchange(uint16_t tx_frame) = 0;

    virtual void SaveState(StateWriter&) {}
    virtual void RestoreState(StateReader&) {}
    virtual void PostRestore() {}
};
