#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

class StateWriter;
class StateReader;

/* `out_mask` is MFIODIREC ($188) & MFIOSEL ($190) - the pins the CHIP drives. MFIODIN
   ($18C) returns a pin's level regardless of direction (TMPR3911 §9.3.2-§9.3.5), so a
   pin outside out_mask carries what this device puts on it via DriveMfioInput. */
class Pr31x00MfioSlave : public Service {
public:
    using Service::Service;

    virtual void OnMfioOut(uint32_t mfio_dout, uint32_t out_mask) = 0;

    virtual void SaveState(StateWriter&) {}
    virtual void RestoreState(StateReader&) {}
};

}
