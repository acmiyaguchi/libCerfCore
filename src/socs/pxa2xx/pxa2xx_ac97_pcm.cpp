#include "pxa2xx_ac97_pcm.h"

#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/cpu/emulated_memory.h>
#include "../../host/audio_activity_widget.h"
#include "../../peripherals/ac97_codec.h"
#include "pxa2xx_ac97_bit_clock.h"

namespace {

/* Intel PXA255 Developer's Manual Table 5-5 (page 5-13): AC97 audio transmit FIFO 0x4050_0040 on DRCMR
   0x4000_0130. */
constexpr uint32_t kPcdr      = 0x40500040u;
constexpr uint32_t kRequestTx = 12u;
constexpr uint16_t kChannels  = 2u;
constexpr uint16_t kBits      = 16u;
constexpr uint32_t kWordBytes = 4u;
constexpr uint32_t kHalfBytes = 2u;
constexpr uint32_t kLeftHalf  = 0u;
constexpr uint32_t kRightHalf = 2u;

}  // namespace

void Pxa2xxAc97Pcm::OnReady() {
    Pxa2xxAc97OutFifo::OnReady();
    bit_clock_ = &emu_.Get<Pxa2xxAc97BitClock>();
    host_.Start("Pxa2xxAc97Pcm", 0u, kChannels, kBits, true);
    emu_.Get<AudioActivityWidget>().NotePresent();
}

void Pxa2xxAc97Pcm::OnShutdown() { host_.Stop(); }

uint32_t Pxa2xxAc97Pcm::FifoPa() const { return kPcdr; }

uint32_t Pxa2xxAc97Pcm::Request() const { return kRequestTx; }

/* AC '97 Component Specification Revision 2.2 section 4.2.1.2 (page 28): "SLOTREQ bits for fixed rate,
   powered down, and all unsupported Slots should be driven with 0s". */
Pxa2xxSlotPace Pxa2xxAc97Pcm::SlotPace() {
    if (codec_->PcmOutEveryFrame()) return Pxa2xxSlotPace{Ac97Codec::kFrameRateHz, 1u};
    return Pxa2xxSlotPace{codec_->DacRateHz(), codec_->ConverterDivider()};
}

/* Intel PXA27x Developer's Manual Table 13-7 (page 13-13): CKEN[31] 1, CKEN[2] 1 "AC97_BITCLK is a 13-MHz clock." */
uint32_t Pxa2xxAc97Pcm::HostRate() {
    const GuestCycleClock::Rate bit = bit_clock_->Internal() ? bit_clock_->Rate() : codec_->BitClockRate();
    const Pxa2xxSlotPace        pace = SlotPace();
    return static_cast<uint32_t>(
        bit.Scaled(pace.rate, static_cast<uint64_t>(pace.div) * Ac97Codec::kBitClockHz).RoundedHz());
}

void Pxa2xxAc97Pcm::OnWord(uint32_t value) { QueueHost(&value, sizeof(value)); }

void Pxa2xxAc97Pcm::OnBlock(uint32_t pa, uint32_t bytes) {
    block_.resize(bytes);
    mem_->CopyOut(pa, block_.data(), bytes);
    QueueHost(block_.data(), bytes);
}

void Pxa2xxAc97Pcm::OnDmaStopped() {
    if (!host_active_) return;
    host_.FinishAudioOut();
    host_active_ = false;
}

void Pxa2xxAc97Pcm::OnRestored() {
    host_.StopAudioOut();
    host_active_ = false;
    host_rate_   = 0u;
}

/* Intel PXA255 Developer's Manual Table 13-14 (page 13-26): PCDR "31:16 PCM_RDATA", "15:0 PCM_LDATA"; Intel
   PXA27x Developer's Manual Table 13-15 (page 13-31): "31:16" PCMR, "15:0" PCML. */
void Pxa2xxAc97Pcm::QueueHost(const void* bytes, uint32_t length) {
    const uint32_t channels = codec_->PcmOutChannels();
    if (channels == 0u) return;
    const uint32_t rate = HostRate();
    if (rate == 0u) return;
    if (channels != (Ac97Codec::kDacLeft | Ac97Codec::kDacRight)) {
        const uint8_t* src = static_cast<const uint8_t*>(bytes);
        masked_.assign(src, src + length);
        const uint32_t muted = (channels & Ac97Codec::kDacLeft) != 0u ? kRightHalf : kLeftHalf;
        for (uint32_t i = muted; i + kHalfBytes <= length; i += kWordBytes) cerf::le::Put16(&masked_[i], 0u);
        bytes = masked_.data();
    }
    if (rate != host_rate_) {
        host_rate_ = rate;
        host_.SetFormat(rate, kChannels, kBits);
    }
    if (!host_active_) {
        host_.BeginAudioOut();
        host_active_ = true;
    }
    host_.QueueOutput(bytes, length);
    emu_.Get<AudioActivityWidget>().MarkTx();
}

REGISTER_SERVICE(Pxa2xxAc97Pcm);
