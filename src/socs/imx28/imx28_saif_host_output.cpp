#include "imx28_saif_host_output.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../host/audio_activity_widget.h"
#include "imx28_id.h"

namespace cerfcore {

namespace {

/* Section 35.3.1 WORD_LENGTH 0000 16-bit, CHANNEL_NUM_SELECT 00 one channel pair. */
constexpr uint16_t kChannels = 2u, kBits = 16u;

}  // namespace

REGISTER_SERVICE(Imx28SaifHostOutput);

bool Imx28SaifHostOutput::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Imx28;
}

void Imx28SaifHostOutput::OnReady() {
    host_.Start("Imx28Saif", 0u, kChannels, kBits, true);
    emu_.Get<AudioActivityWidget>().NotePresent();
}

void Imx28SaifHostOutput::OnShutdown() { host_.Stop(); }

void Imx28SaifHostOutput::Queue(const void* bytes, uint32_t length, uint32_t rate_hz) {
    if (rate_hz != rate_) {
        rate_ = rate_hz;
        host_.SetFormat(rate_hz, kChannels, kBits);
    }
    if (!active_) {
        host_.BeginAudioOut();
        active_ = true;
    }
    host_.QueueOutput(bytes, length);
    emu_.Get<AudioActivityWidget>().MarkTx();
}

void Imx28SaifHostOutput::Finish() {
    if (!active_) return;
    host_.FinishAudioOut();
    active_ = false;
}

void Imx28SaifHostOutput::Stop() {
    host_.StopAudioOut();
    active_ = false;
    rate_   = 0u;
}

}
