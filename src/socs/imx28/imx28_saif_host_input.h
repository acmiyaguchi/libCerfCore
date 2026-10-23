#pragma once

#include <cerfcore/core/service.h>
#include "../../platform/wave_in_fifo.h"

#include <cstdint>

namespace cerfcore {

class Imx28SaifHostInput : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;
    void OnShutdown() override;

    void Open(uint32_t rate_hz);
    void Take(uint32_t* entries, uint64_t count);
    void Clear() { mic_.Clear(); }

private:
    WaveInFifo mic_;
    uint32_t   rate_ = 0u;
};

}
