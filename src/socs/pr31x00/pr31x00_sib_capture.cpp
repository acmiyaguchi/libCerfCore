#include "pr31x00_sib_capture.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../host/audio_activity_widget.h"
#include "pr31x00_sib_codec.h"
#include "pr31500_id.h"
#include "pr31700_id.h"

namespace {

constexpr size_t kMicFifoBytes = 65536u;

constexpr uint32_t kNoPhoneLineWord = 0u;

}

bool Pr31x00SibCapture::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    if (!bd) return false;
    const std::string_view soc = bd->GetSocId();
    return soc == SocId::Pr31500 || soc == SocId::Pr31700;
}

void Pr31x00SibCapture::OnReady() {
    codec_ = &emu_.Get<Pr31x00SibCodec>();
    mic_.Start("SibCapture", kMicFifoBytes);
}

void Pr31x00SibCapture::OnShutdown() {
    mic_.Stop();
}

void Pr31x00SibCapture::Request(uint32_t hz, uint16_t channels) {
    mic_.Request(hz, channels, 16);
}

void Pr31x00SibCapture::Clear() {
    mic_.Clear();
    ahead_.clear();
}

uint32_t Pr31x00SibCapture::Received(uint64_t rx_tick, bool sixteen_bit) {
    while (!ahead_.empty() && ahead_tick_ < rx_tick) {
        ahead_.pop_front();
        ++ahead_tick_;
    }
    if (ahead_.empty()) return codec_->ReceivedWord(Pr31x00SibCircuit::Sound, NextHostWord(sixteen_bit));
    const uint32_t w = ahead_.front();
    ahead_.pop_front();
    ++ahead_tick_;
    return codec_->ReceivedWord(Pr31x00SibCircuit::Sound, w);
}

uint32_t Pr31x00SibCapture::Ahead(uint64_t tick, uint64_t rx_tick, bool sixteen_bit) {
    if (ahead_.empty()) ahead_tick_ = rx_tick;
    while (ahead_tick_ + ahead_.size() <= tick) ahead_.push_back(NextHostWord(sixteen_bit));
    return codec_->ReceivedWord(Pr31x00SibCircuit::Sound,
                                ahead_[static_cast<size_t>(tick - ahead_tick_)]);
}

uint32_t Pr31x00SibCapture::TelecomReceived() {
    return codec_->ReceivedWord(Pr31x00SibCircuit::Telecom, kNoPhoneLineWord);
}

/* philips_nino_300 wavedev.dll sub_1872360 -> sub_187248C byte-swaps each received
   16-bit sample of the DMA buffer, as sub_1871F50 does for transmit. */
uint32_t Pr31x00SibCapture::NextHostWord(bool sixteen_bit) {
    const uint32_t samples = sixteen_bit ? 2u : 4u;
    uint8_t pcm[8];
    if (mic_.Take(pcm, samples * 2u) != 0u) emu_.Get<AudioActivityWidget>().MarkRx();
    uint8_t word[4];
    if (sixteen_bit) {
        for (uint32_t i = 0; i < 2u; ++i)
            cerf::be::Put16(word + i * 2u, cerf::le::U16(pcm, i * 2u));
    } else {
        for (uint32_t i = 0; i < 4u; ++i)
            word[i] = static_cast<uint8_t>(cerf::le::U16(pcm, i * 2u) >> 8);
    }
    return cerf::le::U32(word);
}

REGISTER_SERVICE(Pr31x00SibCapture);
