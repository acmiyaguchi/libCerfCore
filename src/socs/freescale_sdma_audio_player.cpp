#include "freescale_sdma_audio_player.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/cpu/emulated_memory.h>
#include "../host/audio_activity_widget.h"
#include <cerfcore/state/state_stream.h>
#include "freescale_module_clocks.h"
#include "freescale_ssi_core.h"
#include "freescale_ssi_sdma_wiring.h"
#include <cerfcore/socs/guest_cpu_reset.h>

#include <algorithm>

namespace cerfcore {

void FreescaleSdmaAudioPlayer::OnReady() {
    wiring_ = &emu_.Get<FreescaleSsiSdmaWiring>();
    for (uint32_t ssi = 1u; ssi <= wiring_->SsiCount(); ++ssi) {
        for (uint32_t fifo = 0; fifo < 2u; ++fifo) {
            streams_.push_back(std::make_unique<FreescaleSdmaSsiStream>(emu_));
            FreescaleSdmaSsiStream::Hooks hooks;
            hooks.start = [this, ssi, fifo] { OnStart(ssi, fifo); };
            if (fifo == 0u) {
                hooks.open = [this, ssi](uint32_t buffer, uint32_t bytes) {
                    if (ssi == ssi_) QueueSamples(buffer, bytes);
                };
                hooks.halt = [this, ssi] {
                    if (ssi == ssi_) OnHostStreamHalt();
                };
            }
            streams_.back()->Attach(wiring_->Sdma(), LogTag(), std::move(hooks));
        }
    }
    for (uint32_t ssi = 1u; ssi <= wiring_->SsiCount(); ++ssi) {
        wiring_->Core(ssi).RegisterControlListener([this] { OnControl(); });
    }
    emu_.Get<FreescaleModuleClocks>().RegisterGateListener([this] { OnControl(); });
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) { ResetLine(); });
    out_.Start(LogTag(), 0u, 0u, 0u, true);
    wiring_->Sdma().RegisterChannelSink(this);
    emu_.Get<AudioActivityWidget>().NotePresent();
}

void FreescaleSdmaAudioPlayer::OnShutdown() { out_.Stop(); }

std::string FreescaleSdmaAudioPlayer::Prefix(size_t index) {
    return "audio" + std::to_string(index / 2u + 1u) + "_" + std::to_string(index % 2u) + "_";
}

void FreescaleSdmaAudioPlayer::ResetLine() {
    for (auto& s : streams_) s->Reset();
    out_.StopAudioOut();
}

void FreescaleSdmaAudioPlayer::OnControl() {
    if (ssi_ != 0u) ApplyFormat();
    for (auto& s : streams_) s->Rearm();
}

uint32_t FreescaleSdmaAudioPlayer::SampleRateHz(uint32_t ssi, uint32_t channels) {
    if (channels == 0u) return 0u;
    const GuestCycleClock::Rate words = wiring_->Core(ssi).TxFifo0WordRate();
    return static_cast<uint32_t>(words.num / (words.den * channels));
}

void FreescaleSdmaAudioPlayer::ApplyFormat() {
    if (!Stream(ssi_, 0u).Active()) return;
    const FreescaleAudioFormat live = StreamFormat(ssi_);
    if (live.rate_hz == 0u || live.rate_hz == format_.rate_hz) return;
    format_.rate_hz = live.rate_hz;
    out_.SetFormat(format_.rate_hz, format_.channels, format_.bits);
}

bool FreescaleSdmaAudioPlayer::HandlesEvent(int event) const {
    uint32_t ssi = 0, fifo = 0;
    return wiring_->TxEventFifo(event, ssi, fifo);
}

bool FreescaleSdmaAudioPlayer::ClaimChannel(const FreescaleSdmaChannelStart& start) {
    uint32_t ssi = 0, fifo = 0;
    if (!wiring_->TxEventFifo(start.event, ssi, fifo)) return false;
    const FreescaleAudioFormat format = StreamFormat(ssi);
    FreescaleSdmaSsiStream&    s      = Stream(ssi, fifo);
    s.Claim(start, wiring_->Core(ssi).TxPort(fifo), format.bits / 8u);
    LOG(Periph, "[%s] claim SSI%u FIFO %u stream (ch%u ev=%d %u Hz x %u ch x %u bit, %u-word bursts)\n",
        LogTag(), ssi, fifo, start.channel, start.event, format.rate_hz, format.channels,
        format.bits, s.BurstWords());
    return true;
}

void FreescaleSdmaAudioPlayer::OnStart(uint32_t ssi, uint32_t fifo) {
    if (fifo != 0u) return;
    ssi_    = ssi;
    format_ = StreamFormat(ssi);
    out_.SetFormat(format_.rate_hz, format_.channels, format_.bits);
    out_.BeginAudioOut();
}

void FreescaleSdmaAudioPlayer::OnHostStreamHalt() {
    for (uint32_t ssi = 1u; ssi <= wiring_->SsiCount(); ++ssi) {
        if (ssi != ssi_ && Stream(ssi, 0u).Active()) {
            OnStart(ssi, 0u);
            return;
        }
    }
    out_.FinishAudioOut();
}

void FreescaleSdmaAudioPlayer::OnSdmaClock(bool running) {
    for (auto& s : streams_) s->SetSdmaClock(running);
}

void FreescaleSdmaAudioPlayer::ReleaseChannel(uint32_t channel) {
    for (auto& s : streams_) {
        if (s->Release(channel)) return;
    }
}

void FreescaleSdmaAudioPlayer::QueueSamples(uint32_t buf_pa, uint32_t bytes) {
    if (bytes == 0u || format_.rate_hz == 0u) return;
    auto&   mem = emu_.Get<EmulatedMemory>();
    uint8_t chunk[PacedWaveOut::kMaxBlock];
    for (uint32_t off = 0; off < bytes;) {
        const uint32_t n = std::min<uint32_t>(bytes - off, PacedWaveOut::kMaxBlock);
        for (uint32_t i = mem.TryCopyOut(buf_pa + off, chunk, n); i < n; ++i) {
            chunk[i] = mem.ReadByte(buf_pa + off + i);
        }
        out_.QueueOutput(chunk, n);
        off += n;
    }
    emu_.Get<AudioActivityWidget>().MarkTx();
}

void FreescaleSdmaAudioPlayer::SaveSinkState(StateWriter& w) {
    w.Write("audio_ssi", ssi_);
    w.Write("audio_rate", format_.rate_hz);
    w.Write("audio_channels", format_.channels);
    w.Write("audio_bits", format_.bits);
    for (size_t i = 0; i < streams_.size(); ++i) streams_[i]->Save(w, Prefix(i).c_str());
}

void FreescaleSdmaAudioPlayer::RestoreSinkState(StateReader& r) {
    r.Read("audio_ssi", ssi_);
    r.Read("audio_rate", format_.rate_hz);
    r.Read("audio_channels", format_.channels);
    r.Read("audio_bits", format_.bits);
    for (size_t i = 0; i < streams_.size(); ++i) {
        const uint32_t ssi  = static_cast<uint32_t>(i / 2u) + 1u;
        const uint32_t fifo = static_cast<uint32_t>(i % 2u);
        streams_[i]->Restore(r, Prefix(i).c_str(), &wiring_->Core(ssi).TxPort(fifo));
    }
    out_.StopAudioOut();
    if (ssi_ == 0u || !Stream(ssi_, 0u).Active()) return;
    out_.SetFormat(format_.rate_hz, format_.channels, format_.bits);
    out_.BeginAudioOut();
}

void FreescaleSdmaAudioPlayer::PostRestoreSink() {
    for (auto& s : streams_) s->PostRestore();
}

}
