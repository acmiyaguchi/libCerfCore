#pragma once

#include <cerfcore/core/service.h>
#include "../../host/host_pcm_session.h"

#include <cstdint>

namespace cerfcore {

class Imx28SaifHostOutput : public Service {
public:
    explicit Imx28SaifHostOutput(CerfEmulator& emu) : Service(emu) {}

    bool ShouldRegister() override;
    void OnReady() override;
    void OnShutdown() override;

    void Queue(const void* bytes, uint32_t length, uint32_t rate_hz) {
        session_.Queue(bytes, length, rate_hz);
    }
    void Finish() { session_.Finish(); }
    void Stop() { session_.Abort(); }

private:
    /* Section 35.3.1 WORD_LENGTH 0000 16-bit, CHANNEL_NUM_SELECT 00 one channel pair. */
    static constexpr uint16_t kChannels = 2u, kBits = 16u;

    HostPcmSession session_{emu_, "Imx28Saif", kChannels, kBits};
};

}
