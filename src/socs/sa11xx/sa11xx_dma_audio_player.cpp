#define NOMINMAX

#include "sa11xx_dma_audio_player.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/cpu/emulated_memory.h>
#include "../../host/audio_activity_widget.h"
#include "sa11xx_dma.h"

#include <cstring>

namespace cerfcore {

void Sa11xxDmaAudioPlayer::OnReady() {
    cfg_ = AudioConfig();
    out_.Start(cfg_.log_tag, 0u, cfg_.channels, cfg_.bits_per_sample, cfg_.allow_resampler);
    emu_.Get<Sa11xxDma>().RegisterTransmitObserver(this);
    emu_.Get<AudioActivityWidget>().NotePresent();
}

void Sa11xxDmaAudioPlayer::OnShutdown() { out_.Stop(); }

void Sa11xxDmaAudioPlayer::OnTransmitBlock(uint32_t ddar, uint32_t pa, uint32_t bytes,
                                           GuestCycleClock::Rate word_rate) {
    if (!Matches(ddar)) return;
    block_.resize(bytes);
    emu_.Get<EmulatedMemory>().CopyOut(pa, block_.data(), bytes);
    QueueBlock(word_rate);
}

void Sa11xxDmaAudioPlayer::OnTransmitData(uint32_t ddar, const uint8_t* data, uint32_t bytes,
                                          GuestCycleClock::Rate word_rate) {
    if (!Matches(ddar)) return;
    block_.assign(data, data + bytes);
    QueueBlock(word_rate);
}

void Sa11xxDmaAudioPlayer::QueueBlock(GuestCycleClock::Rate word_rate) {
    const uint32_t rate = static_cast<uint32_t>(word_rate.Scaled(1u, cfg_.channels).RoundedHz());
    if (rate != rate_) {
        rate_ = rate;
        out_.SetFormat(rate, cfg_.channels, cfg_.bits_per_sample);
    }
    if (!active_) {
        out_.BeginAudioOut();
        active_ = true;
    }
    if (OutputMuted()) std::memset(block_.data(), 0, block_.size());
    else               emu_.Get<AudioActivityWidget>().MarkTx();
    out_.QueueOutput(block_.data(), static_cast<uint32_t>(block_.size()));
}

void Sa11xxDmaAudioPlayer::OnTransmitStop(uint32_t ddar) {
    if (!Matches(ddar) || !active_) return;
    out_.FinishAudioOut();
    active_ = false;
}

void Sa11xxDmaAudioPlayer::OnTransmitRestored() {
    out_.StopAudioOut();
    active_ = false;
    rate_   = 0u;
}

}
