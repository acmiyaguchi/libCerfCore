#include "host_pcm_session.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include "audio_activity_widget.h"

namespace cerfcore {

void HostPcmSession::ReportStream() {
    LOG(Cerf, "[%s] pcm rate=%u bytes=%llu peak=%u\n", log_tag_, rate_,
        static_cast<unsigned long long>(stream_bytes_), stream_peak_);
    last_report_ = std::chrono::steady_clock::now();
}

void HostPcmSession::Open() {
    host_.Start(log_tag_, 0u, channels_, bits_, true);
    emu_.Get<AudioActivityWidget>().NotePresent();
}

void HostPcmSession::Close() { host_.Stop(); }

void HostPcmSession::Queue(const void* bytes, uint32_t length, uint32_t rate_hz) {
    if (rate_hz != rate_) {
        rate_ = rate_hz;
        host_.SetFormat(rate_hz, channels_, bits_);
    }
    if (!active_) {
        host_.BeginAudioOut();
        active_       = true;
        stream_bytes_ = 0;
        stream_peak_  = 0;
        last_report_  = std::chrono::steady_clock::now();
    }
    stream_bytes_ += length;
    if (bits_ == 16u) {
        const int16_t* samples = static_cast<const int16_t*>(bytes);
        for (uint32_t i = 0; i < length / 2u; i++) {
            const uint32_t mag = samples[i] < 0 ? -static_cast<int32_t>(samples[i]) : samples[i];
            if (mag > stream_peak_) stream_peak_ = mag;
        }
    }
    if (std::chrono::steady_clock::now() - last_report_ >= std::chrono::seconds(1)) ReportStream();
    host_.QueueOutput(bytes, length);
    emu_.Get<AudioActivityWidget>().MarkTx();
}

void HostPcmSession::Finish() {
    if (!active_) return;
    host_.FinishAudioOut();
    ReportStream();
    active_ = false;
}

void HostPcmSession::Abort() {
    if (active_) ReportStream();
    host_.StopAudioOut();
    active_ = false;
    rate_   = 0u;
}

}
