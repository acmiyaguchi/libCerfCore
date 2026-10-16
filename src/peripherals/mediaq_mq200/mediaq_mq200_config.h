#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

class MediaQMq200Config : public Service {
public:
    using Service::Service;

    virtual uint32_t HostWindowBase() const = 0;

    virtual uint32_t OscillatorHz() const = 0;

    /* MQ-200 Data Book Table 5-95: DC01R[5:0] bus interface mode. */
    virtual uint32_t BusInterfaceMode() const = 0;
};
