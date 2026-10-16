#include "pxa255_i2s_host_output.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../host/audio_activity_widget.h"
#include "pxa255_id.h"

namespace cerfcore {

namespace {

/* Intel PXA255 Developer's Manual section 14.5.1 (page 14-6): "two samples (Left + Right) per 32-bit word";
   section 14.2 (page 14-2): SYNC is BITCLK divided by 64. */
constexpr uint16_t kChannels   = 2u, kBits = 16u;
constexpr uint64_t kBitsPerFrame = 64u;

}  // namespace

REGISTER_SERVICE(Pxa255I2sHostOutput);

bool Pxa255I2sHostOutput::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Pxa255;
}

void Pxa255I2sHostOutput::OnReady() {
    host_.Start("Pxa255I2s", 0u, kChannels, kBits, true);
    emu_.Get<AudioActivityWidget>().NotePresent();
}

void Pxa255I2sHostOutput::OnShutdown() { host_.Stop(); }

void Pxa255I2sHostOutput::Queue(const void* bytes, uint32_t length, GuestCycleClock::Rate bit_rate) {
    const uint32_t rate = static_cast<uint32_t>(bit_rate.Scaled(1u, kBitsPerFrame).RoundedHz());
    if (rate == 0u) return;
    if (rate != rate_) {
        rate_ = rate;
        host_.SetFormat(rate, kChannels, kBits);
    }
    if (!active_) {
        host_.BeginAudioOut();
        active_ = true;
    }
    host_.QueueOutput(bytes, length);
    emu_.Get<AudioActivityWidget>().MarkTx();
}

void Pxa255I2sHostOutput::Finish() {
    if (!active_) return;
    host_.FinishAudioOut();
    active_ = false;
}

void Pxa255I2sHostOutput::Stop() {
    host_.StopAudioOut();
    active_ = false;
    rate_   = 0u;
}

}
