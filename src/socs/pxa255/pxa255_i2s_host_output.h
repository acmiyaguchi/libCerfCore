#pragma once

#include <cerfcore/core/service.h>
#include "../../host/paced_wave_out.h"
#include "../../execution/guest_cycle_clock.h"

#include <cstdint>

class Pxa255I2sHostOutput : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;
    void OnShutdown() override;

    void Queue(const void* bytes, uint32_t length, GuestCycleClock::Rate bit_rate);
    void Finish();
    void Stop();

private:
    PacedWaveOut host_;
    bool         active_ = false;
    uint32_t     rate_   = 0;
};
