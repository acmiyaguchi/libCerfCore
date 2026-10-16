#include "freescale_sdma_ssi_stream.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/state/state_stream.h>

#include <algorithm>
#include <string>
#include <utility>

void FreescaleSdmaSsiStream::Attach(FreescaleSdmaChannelHost& host, const char* tag, Hooks hooks) {
    host_  = &host;
    tag_   = tag;
    hooks_ = std::move(hooks);
    clock_ = &emu_.Get<GuestCycleClock>();
    event_ = clock_->Add([this] { OnBdEnd(); });
}

void FreescaleSdmaSsiStream::Reset() {
    active_ = false;
    clock_->Disarm(event_);
}

/* MCIMX31RM §40.12.3.5 NOTE: a channel moves "a number of data that matches the watermark
   level"; the guests load the watermark in bytes as context GR7 (zune_keel cspddk.dll
   sub_31540C4 and sync_2 cspddk.dll sub_C09C4640: ctx[9] = watermark). */
void FreescaleSdmaSsiStream::Claim(const FreescaleSdmaChannelStart& start, FreescaleSsiDmaPort& port,
                                   uint32_t sample_bytes) {
    if (sample_bytes == 0u) {
        emu_.Get<Fatal>().Die("%s: SDMA channel %u (event %d) starts with a 0-byte sample", tag_,
                              start.channel, start.event);
    }
    if (active_) Stop();
    ring_.Begin(start);
    uint32_t   watermark = 0;
    const bool context   = host_->ChannelWatermark(start.channel, watermark);
    port_         = &port;
    sample_bytes_ = sample_bytes;
    burst_words_  = context ? std::max<uint32_t>(1u, watermark / sample_bytes) : 1u;
    bd_end_words_ = 0u;
    active_       = true;
    port_->SetDmaClock(sdma_clocked_);
    port_->SetDmaBurst(burst_words_);
    if (hooks_.start) hooks_.start();
    StartBd();
}

bool FreescaleSdmaSsiStream::Release(uint32_t channel) {
    if (!active_ || channel != ring_.Channel()) return false;
    active_ = false;
    port_->SetDmaBurst(0u);
    clock_->Disarm(event_);
    if (hooks_.halt) hooks_.halt();
    return true;
}

void FreescaleSdmaSsiStream::Stop() {
    if (!active_) return;
    active_ = false;
    port_->SetDmaBurst(0u);
    clock_->Disarm(event_);
}

/* MCIMX51RM page 52-231: "The SDMA script cannot process a BD with a Done bit to 0"; Linux imx-sdma
   sdma_update_channel_loop: "SDMA stops cyclic channel when DMA request triggers a channel and no
   SDMA owned buffer is available". */
void FreescaleSdmaSsiStream::StartBd() {
    FreescaleSdmaBd bd;
    if (!ring_.Open(bd)) {
        Halt();
        host_->StopChannel(ring_.Channel());
        LOG(Periph, "[%s] SDMA channel %u stops at unowned BD %08X\n", tag_, ring_.Channel(),
            ring_.Current());
        return;
    }
    bd_cont_   = bd.cont;
    bd_buffer_ = bd.buffer;
    bd_words_  = bd.count / sample_bytes_;
    if (hooks_.open) hooks_.open(bd_buffer_, bd_words_ * sample_bytes_);
    bd_end_words_ += bd_words_;
    Rearm();
}

void FreescaleSdmaSsiStream::SetSdmaClock(bool running) {
    sdma_clocked_ = running;
    if (!active_) return;
    port_->SetDmaClock(running);
    Rearm();
}

void FreescaleSdmaSsiStream::Rearm() {
    uint64_t cycle = 0;
    if (active_ && port_->CycleOfDmaWords(bd_end_words_, cycle)) {
        clock_->Arm(event_, cycle);
    } else {
        clock_->Disarm(event_);
    }
}

void FreescaleSdmaSsiStream::OnBdEnd() {
    const uint64_t moved = port_->DmaWords();
    if (moved < bd_end_words_) {
        emu_.Get<Fatal>().Die("%s: SDMA channel %u BD end event after %llu of %llu words", tag_,
                              ring_.Channel(), static_cast<unsigned long long>(moved),
                              static_cast<unsigned long long>(bd_end_words_));
    }
    if (hooks_.retire) hooks_.retire(bd_buffer_, bd_words_ * sample_bytes_);
    const uint32_t done_bd = ring_.Current();
    if (!ring_.Retire(*host_, bd_cont_)) {
        Halt();
        LOG(Periph, "[%s] SDMA channel %u ends after BD %08X (C clear)\n", tag_, ring_.Channel(),
            done_bd);
        return;
    }
    StartBd();
}

void FreescaleSdmaSsiStream::Halt() {
    active_ = false;
    port_->SetDmaBurst(0u);
    clock_->Disarm(event_);
    if (hooks_.halt) hooks_.halt();
}

void FreescaleSdmaSsiStream::Save(StateWriter& w, const char* prefix) {
    const std::string p(prefix);
    w.Write<uint8_t>((p + "active").c_str(), active_ ? 1u : 0u);
    ring_.Save(w, prefix);
    w.Write((p + "sample_bytes").c_str(), sample_bytes_);
    w.Write((p + "burst_words").c_str(), burst_words_);
    w.Write((p + "bd_buffer").c_str(), bd_buffer_);
    w.Write((p + "bd_words").c_str(), bd_words_);
    w.Write((p + "bd_end_words").c_str(), bd_end_words_);
    w.Write<uint8_t>((p + "bd_cont").c_str(), bd_cont_ ? 1u : 0u);
}

void FreescaleSdmaSsiStream::Restore(StateReader& r, const char* prefix, FreescaleSsiDmaPort* port) {
    const std::string p(prefix);
    uint8_t active = 0, cont = 0;
    r.Read((p + "active").c_str(), active);
    ring_.Restore(r, prefix);
    r.Read((p + "sample_bytes").c_str(), sample_bytes_);
    r.Read((p + "burst_words").c_str(), burst_words_);
    r.Read((p + "bd_buffer").c_str(), bd_buffer_);
    r.Read((p + "bd_words").c_str(), bd_words_);
    r.Read((p + "bd_end_words").c_str(), bd_end_words_);
    r.Read((p + "bd_cont").c_str(), cont);
    active_  = active != 0u;
    bd_cont_ = cont != 0u;
    port_    = port;
    clock_->Disarm(event_);
}

void FreescaleSdmaSsiStream::PostRestore() {
    if (!active_) return;
    const uint64_t done = port_->DmaWords() - (bd_end_words_ - bd_words_);
    const uint32_t from = static_cast<uint32_t>(done) * sample_bytes_;
    if (hooks_.open) hooks_.open(bd_buffer_ + from, bd_words_ * sample_bytes_ - from);
    Rearm();
}
