#include "imx28_saif_host_input.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../host/audio_activity_widget.h"
#include "imx28_id.h"

#include <cstring>

namespace cerfcore {

namespace {

constexpr uint16_t kChannels     = 1u;
constexpr uint16_t kBits         = 16u;
constexpr uint32_t kSampleBytes  = 2u;
constexpr uint64_t kChunkSamples = 256u;
constexpr size_t   kMicFifoBytes = 65536u;

}  /* namespace */

REGISTER_SERVICE(Imx28SaifHostInput);

bool Imx28SaifHostInput::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Imx28;
}

void Imx28SaifHostInput::OnReady() { mic_.Start("Imx28SaifCapture", kMicFifoBytes); }

void Imx28SaifHostInput::OnShutdown() { mic_.Stop(); }

void Imx28SaifHostInput::Open(uint32_t rate_hz) {
    rate_ = rate_hz;
    mic_.Request(rate_, kChannels, kBits);
}

/* MCIMX28RM Rev. 2 section 35.2.3: 16-bit pairs, right sample in the upper halfword;
   sharp_brain_pw_sh4 wavedev2_BU26154.dll c07a9f58 takes only entries whose halves match. */
void Imx28SaifHostInput::Take(uint32_t* entries, uint64_t count) {
    int16_t pcm[kChunkSamples];
    bool    got = false;
    while (count != 0u) {
        const uint64_t n = count < kChunkSamples ? count : kChunkSamples;
        std::memset(pcm, 0, sizeof(pcm));
        if (rate_ != 0u && mic_.Take(reinterpret_cast<uint8_t*>(pcm), n * kSampleBytes) != 0u) got = true;
        for (uint64_t i = 0; i < n; ++i) {
            const uint32_t s = static_cast<uint16_t>(pcm[i]);
            if (entries != nullptr) *entries++ = s | (s << 16);
        }
        count -= n;
    }
    if (got) emu_.Get<AudioActivityWidget>().MarkRx();
}

}
