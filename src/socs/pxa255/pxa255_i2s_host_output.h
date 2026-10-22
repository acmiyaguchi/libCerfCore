#pragma once

#include <cerfcore/core/service.h>
#include "../../host/host_pcm_session.h"
#include "../../execution/guest_cycle_clock.h"

#include <cstdint>

namespace cerfcore {

class Pxa255I2sHostOutput : public Service {
public:
    explicit Pxa255I2sHostOutput(CerfEmulator& emu) : Service(emu) {}

    bool ShouldRegister() override;
    void OnReady() override;
    void OnShutdown() override;

    void Queue(const void* bytes, uint32_t length, GuestCycleClock::Rate bit_rate);
    void Finish() { session_.Finish(); }
    void Stop() { session_.Abort(); }

private:
    /* Intel PXA255 Developer's Manual section 14.5.1 (page 14-6): Left and Right samples per
       32-bit word. */
    static constexpr uint16_t kChannels = 2u, kBits = 16u;

    HostPcmSession session_{emu_, "Pxa255I2s", kChannels, kBits};
};

}
