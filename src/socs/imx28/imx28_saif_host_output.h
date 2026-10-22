#pragma once

#include <cerfcore/core/service.h>
#include "../../host/paced_wave_out.h"

#include <cstdint>

namespace cerfcore {

class Imx28SaifHostOutput : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;
    void OnShutdown() override;

    void Queue(const void* bytes, uint32_t length, uint32_t rate_hz);
    void Finish();
    void Stop();

private:
    PacedWaveOut host_;
    bool         active_ = false;
    uint32_t     rate_   = 0;
};

}
