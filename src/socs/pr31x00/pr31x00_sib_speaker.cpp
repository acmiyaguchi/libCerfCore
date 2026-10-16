#include "pr31x00_sib_speaker.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../host/audio_activity_widget.h"
#include "pr31x00_sib_codec.h"
#include "pr31500_id.h"
#include "pr31700_id.h"

namespace cerfcore {

bool Pr31x00SibSpeaker::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    if (!bd) return false;
    const std::string_view soc = bd->GetSocId();
    return soc == SocId::Pr31500 || soc == SocId::Pr31700;
}

void Pr31x00SibSpeaker::OnReady() {
    codec_ = &emu_.Get<Pr31x00SibCodec>();
    paced_.Start("SibAudio", 0, 1, 16, true);
    emu_.Get<AudioActivityWidget>().NotePresent();
}

void Pr31x00SibSpeaker::OnShutdown() { paced_.Stop(); }

void Pr31x00SibSpeaker::SetFormat(uint32_t hz, uint16_t channels) {
    paced_.SetFormat(hz, channels, 16);
}

void Pr31x00SibSpeaker::Begin() { paced_.BeginAudioOut(); }

void Pr31x00SibSpeaker::Finish() { paced_.FinishAudioOut(); }

void Pr31x00SibSpeaker::Abort() { paced_.StopAudioOut(); }

void Pr31x00SibSpeaker::AddWord(uint32_t hold, bool sixteen_bit) {
    const uint32_t parts   = sixteen_bit ? 2u : 4u;
    const uint32_t width   = 32u / parts;
    const bool     audible = codec_->AudioOutputAudible();
    for (uint32_t i = 0; i < parts; ++i) {
        const uint32_t field  = (hold >> (32u - width * (i + 1u))) & ((1u << width) - 1u);
        const uint32_t sample = !audible ? 0u : sixteen_bit ? field : field << 8;
        cerf::le::Append16(buf_, static_cast<uint16_t>(sample));
    }
}

void Pr31x00SibSpeaker::Flush() {
    emu_.Get<AudioActivityWidget>().MarkTx();
    paced_.QueueOutputInHostBlocks(buf_.data(), static_cast<uint32_t>(buf_.size()));
    buf_.clear();
}

REGISTER_SERVICE(Pr31x00SibSpeaker);

}
