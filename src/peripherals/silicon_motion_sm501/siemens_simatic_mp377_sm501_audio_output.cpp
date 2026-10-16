#include "siemens_simatic_mp377_sm501_audio_output.h"

#include <cerfcore/boards/board_context.h>
#include "../../boards/siemens_simatic_mp377/siemens_simatic_mp377_family_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include "../../host/audio_activity_widget.h"
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>

namespace cerfcore {

namespace siemens_simatic_mp377 {

bool SiemensSimaticMp377Sm501AudioOutput::ShouldRegister() {
    auto* board = emu_.TryGet<BoardContext>();
    return board && board->GetBoardFamilyId() == BoardFamilyId::SiemensSimaticMp377;
}

void SiemensSimaticMp377Sm501AudioOutput::OnReady() {
    widget_ = &emu_.Get<AudioActivityWidget>();
    output_.Start("MP377SM501Audio", 0, 2, 16, true);
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
        if (emu_.Get<GuestCpuReset>().DeliveredResetWasResume()) return;
        ResetDevice();
    });
}

void SiemensSimaticMp377Sm501AudioOutput::OnShutdown() {
    output_.StopAudioOut();
    output_.Stop();
}

void SiemensSimaticMp377Sm501AudioOutput::QueueAc97PcmSample(uint32_t left20, uint32_t right20) {
    if (!active_.load(std::memory_order_acquire)) return;
    const int16_t left = static_cast<int16_t>((left20 >> 4u) & 0xFFFFu);
    const int16_t right = static_cast<int16_t>((right20 >> 4u) & 0xFFFFu);
    std::lock_guard<std::mutex> lock(pcm_mutex_);
    uint8_t* out = host_packet_.data() + host_packet_frames_ * 4u;
    out[0] = static_cast<uint8_t>(left);
    out[1] = static_cast<uint8_t>(static_cast<uint16_t>(left) >> 8u);
    out[2] = static_cast<uint8_t>(right);
    out[3] = static_cast<uint8_t>(static_cast<uint16_t>(right) >> 8u);
    ++host_packet_frames_;
    if (host_packet_frames_ == kHostPacketFrames) {
        output_.QueueOutput(host_packet_.data(), kHostPacketBytes);
        widget_->NotePresent();
        host_packet_.fill(0u);
        host_packet_frames_ = 0u;
    }
}

void SiemensSimaticMp377Sm501AudioOutput::HandleDacPowerDown() {
    if (!active_.load(std::memory_order_acquire)) return;
    active_.store(false, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(pcm_mutex_);
        host_packet_frames_ = 0u;
        host_packet_.fill(0u);
    }
    output_.FinishAudioOut();
}

void SiemensSimaticMp377Sm501AudioOutput::SetPlaybackEnabled(bool enabled) {
    if (enabled && !active_.load(std::memory_order_acquire)) {
        active_.store(true, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lock(pcm_mutex_);
            host_packet_.fill(0u);
            host_packet_frames_ = 0u;
        }
        output_.SetFormat(Ac97Codec::kFrameRateHz, 2, 16);
        output_.BeginAudioOut();
        widget_->NotePresent();
    } else if (!enabled && active_.load(std::memory_order_acquire)) {
        active_.store(false, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lock(pcm_mutex_);
            host_packet_.fill(0u);
            host_packet_frames_ = 0u;
        }
        output_.FinishAudioOut();
    }
}

void SiemensSimaticMp377Sm501AudioOutput::SaveState(StateWriter& w) {
    w.Write("active", active_.load(std::memory_order_acquire));
    {
        std::lock_guard<std::mutex> lock(pcm_mutex_);
        w.WriteBytes("host_packet", host_packet_.data(), host_packet_.size());
        w.Write("host_packet_frames", host_packet_frames_);
    }
}

void SiemensSimaticMp377Sm501AudioOutput::RestoreState(StateReader& r) {
    bool active = false;
    r.Read("active", active);
    active_.store(active, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(pcm_mutex_);
        r.ReadBytes("host_packet", host_packet_.data(), host_packet_.size());
        r.Read("host_packet_frames", host_packet_frames_);
        if (host_packet_frames_ > kHostPacketFrames)
            r.Reject("host packet frames %u past %u", host_packet_frames_, kHostPacketFrames);
    }
    output_.StopAudioOut();
    if (active) {
        output_.SetFormat(Ac97Codec::kFrameRateHz, 2, 16);
        output_.BeginAudioOut();
    }
}

void SiemensSimaticMp377Sm501AudioOutput::ResetDevice() {
    active_.store(false, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(pcm_mutex_);
        host_packet_frames_ = 0u;
        host_packet_.fill(0u);
    }
    output_.StopAudioOut();
}

REGISTER_SERVICE(SiemensSimaticMp377Sm501AudioOutput);

} // namespace siemens_mp377

}
