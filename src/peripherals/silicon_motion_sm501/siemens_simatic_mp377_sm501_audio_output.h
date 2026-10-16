#pragma once

#include <cerfcore/core/service.h>
#include "../../host/paced_wave_out.h"
#include "../ac97_codec.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

class AudioActivityWidget;
class StateReader;
class StateWriter;

namespace siemens_simatic_mp377 {

// SM501 Databook ch. 11; siemens_mp377_v1040 VGXaudio.dll sub_2987AAC.
class SiemensSimaticMp377Sm501AudioOutput : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;
    void OnShutdown() override;

    void QueueAc97PcmSample(uint32_t left20, uint32_t right20);
    void HandleDacPowerDown();
    void SetPlaybackEnabled(bool enabled);

    void SaveState(StateWriter& writer);
    void RestoreState(StateReader& reader);

private:
    static constexpr uint32_t kFramesPerFirmwareHalf = 192u;
    static constexpr uint32_t kHostPacketFrames = kFramesPerFirmwareHalf;
    static constexpr uint32_t kHostPacketBytes = kHostPacketFrames * 4u;

    void ResetDevice();

    PacedWaveOut output_;
    AudioActivityWidget* widget_ = nullptr;
    std::atomic<bool> active_{false};
    std::array<uint8_t, kHostPacketBytes> host_packet_{};
    uint32_t host_packet_frames_ = 0u;
    std::mutex pcm_mutex_;
};

} // namespace siemens_mp377
