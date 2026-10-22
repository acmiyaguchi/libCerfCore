#include "host_pcm_session.h"

#include <cerfcore/core/cerf_emulator.h>
#include "audio_activity_widget.h"

namespace cerfcore {

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
        active_ = true;
    }
    host_.QueueOutput(bytes, length);
    emu_.Get<AudioActivityWidget>().MarkTx();
}

void HostPcmSession::Finish() {
    if (!active_) return;
    host_.FinishAudioOut();
    active_ = false;
}

void HostPcmSession::Abort() {
    host_.StopAudioOut();
    active_ = false;
    rate_   = 0u;
}

}
