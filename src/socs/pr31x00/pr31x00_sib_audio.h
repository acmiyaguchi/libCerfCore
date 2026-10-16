#pragma once

#include <cerfcore/core/service.h>
#include "../../execution/guest_cycle_clock.h"

#include <cstdint>

class StateWriter;
class StateReader;

enum class Pr31x00SibCircuit : uint8_t { Sound, Telecom };

struct Pr31x00SibDmaSetup {
    uint32_t              tx_start_pa = 0;
    uint32_t              rx_start_pa = 0;
    uint32_t              size        = 0;
    bool                  tx          = false;
    bool                  rx          = false;
    bool                  rx_first    = false;
    bool                  one_time    = false;
    bool                  sixteen_bit = true;
    bool                  stereo      = false;
    bool                  muted       = false;
    GuestCycleClock::Rate frame_rate;
};

class Pr31x00SibAudioSink : public Service {
public:
    using Service::Service;

    virtual void     StartDma(Pr31x00SibCircuit circuit, const Pr31x00SibDmaSetup& dma) = 0;
    virtual void     UpdateDma(Pr31x00SibCircuit circuit, const Pr31x00SibDmaSetup& dma) = 0;
    virtual void     StopDma(Pr31x00SibCircuit circuit) = 0;
    virtual uint32_t DmaPointer(Pr31x00SibCircuit circuit) = 0;
    virtual void     FlushReceived() = 0;

    virtual void SaveState(StateWriter& w) = 0;
    virtual void RestoreState(StateReader& r) = 0;
};
