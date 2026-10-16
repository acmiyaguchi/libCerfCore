#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

/* Board-side consumer of SIMpad CS3 latch writes - the PC Card socket's Vcc
   enables and RESET line live in the CS3 latch (@ PA 0x1A000000). Optional -
   TryGet'd by the CS3 latch peripheral on each write. */
class SiemensSimpadCs3Sink : public Service {
public:
    using Service::Service;
    ~SiemensSimpadCs3Sink() override = default;

    virtual void OnCs3LatchChanged(uint16_t latch) = 0;
};

}
