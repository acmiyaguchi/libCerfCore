#include "casio_jx670_audio.h"

#include "casio_jx670_capture.h"
#include "casio_jx670_rate_ladder.h"
#include "casio_jx670_speaker.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include "../../host/paced_wave_out.h"
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>

#include <algorithm>
#include <iterator>

void CasioJx670Audio::Init(CerfEmulator& emu,
                                     std::function<void()> on_irq_change) {
    emu_ = &emu;
    on_irq_change_ = std::move(on_irq_change);
    clock_ = &emu.Get<GuestCycleClock>();
    event_ = clock_->Add([this] { OnBlockEnd(); });
    clock_->RegisterRateListener([this] { OnRateChange(); });
    auto& reset = emu.Get<GuestCpuReset>();
    reset.RegisterResetListener([this](ResetLineKind) { OnResetLine(); });
    reset.RegisterResetReleaseListener([this] { on_irq_change_(); });
    speaker_ = &emu.Get<CasioJx670Speaker>();
    mic_     = &emu.Get<CasioJx670Capture>();
    ladder_  = &emu.Get<CasioJx670RateLadder>();
}

void CasioJx670Audio::OnResetLine() {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        clock_->Disarm(event_);
        reg_880_ = reg_884_ = reg_888_ = reg_890_ = reg_898_ = reg_8A0_ = 0u;
        reg_8C4_ = reg_8C8_ = reg_8CC_ = 0u;
        std::fill(std::begin(desc_), std::end(desc_), 0u);
        std::fill(std::begin(blocks_), std::end(blocks_), Block{});
        queued_   = 0u;
        ran_dry_  = false;
        rate_hz_  = 0u;
        channels_ = 0u;
        capture_  = false;
    }
    status_8A8_.store(0u, std::memory_order_release);
    speaker_->Abort();
}

void CasioJx670Audio::SetRateDoubler(bool on) {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        rate_doubler_ = on;
    }
    RequireStreamFormat();
}

bool CasioJx670Audio::TryReadHalf(uint32_t off, uint16_t& out) {
    if (off == kOffStatus8A8) {
        out = status_8A8_.exchange(0u, std::memory_order_acq_rel);
        on_irq_change_();
        return true;
    }
    std::lock_guard<std::mutex> lk(mtx_);
    switch (off) {
        case kOffCtrl880:   out = static_cast<uint16_t>(reg_880_); return true;
        case kOffEnable884:
            out = static_cast<uint16_t>(
                reg_884_ | (queued_ != 0u ? kEnableBusyBit : 0u));
            return true;
        case kOffFormat888: out = static_cast<uint16_t>(reg_888_); return true;
        case kOffLatch8A0:  out = static_cast<uint16_t>(reg_8A0_); return true;
        default: return false;
    }
}

bool CasioJx670Audio::TryWriteHalf(uint32_t off, uint16_t value) {
    const auto merge = [value](uint32_t& reg) { reg = (reg & 0xFFFF0000u) | value; };
    if (off == kOffLatch8A0) { OnLatchWrite(value, 0xFFFF0000u); return true; }
    if (off == kOffCtrl880 || off == kOffFormat888) {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            merge(off == kOffCtrl880 ? reg_880_ : reg_888_);
        }
        RequireStreamFormat();
        return true;
    }
    std::lock_guard<std::mutex> lk(mtx_);
    switch (off) {
        /* dword_F622D4 @0xF62384 lhu 0x884 / @0xF62386 or 1 / @0xF62388 sh, loc_F61D94
           @0xF61DB2-@0xF61DB8, sub_F62520 @0xF6257E-@0xF62586 and loc_F62984
           @0xF62A38-@0xF62A3E / @0xF62A7E-@0xF62A86 read-modify-write 0x0884 with no
           mask clearing bit2. */
        case kOffEnable884:
            reg_884_ = (reg_884_ & 0xFFFF0000u) |
                       (value & static_cast<uint16_t>(~kEnableBusyBit));
            return true;
        case kOffAckL8C8:   merge(reg_8C8_); return true;
        case kOffAckR8CC:   merge(reg_8CC_); return true;
        default: return false;
    }
}

bool CasioJx670Audio::TryReadWord(uint32_t off, uint32_t& out) {
    std::lock_guard<std::mutex> lk(mtx_);
    switch (off) {
        case kOffCtrl880:   out = reg_880_; return true;
        case kOffEnable884:
            out = reg_884_ | (queued_ != 0u ? kEnableBusyBit : 0u);
            return true;
        case kOffFormat888: out = reg_888_; return true;
        case kOffChan890:   out = reg_890_; return true;
        case kOffStrobe898: out = reg_898_; return true;
        case kOffLatch8A0:  out = reg_8A0_; return true;
        case kOffGate8C4:   out = reg_8C4_; return true;
        default: return false;
    }
}

bool CasioJx670Audio::TryWriteWord(uint32_t off, uint32_t value) {
    if (off >= kOffDescLo && off <= kOffDescHi && (off & 3u) == 0u) {
        const uint32_t index = (off - kOffDescLo) / 4u;
        uint32_t in_flight = 0;
        bool     stored    = false;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            in_flight = queued_;
            stored = queued_ == 0u ||
                     (Running() && Streaming() && index >= kDescNextStart && queued_ == 1u);
            if (stored) desc_[index] = value;
        }
        if (!stored) {
            emu_->Get<Fatal>().Die("EM-500 audio: DMA descriptor 0x%04X write 0x%08X with %u "
                                   "blocks in flight; not modeled", off, value, in_flight);
        }
        if (index == kDescNextEnd) QueueDescriptor(kDescNextStart);
        return true;
    }
    switch (off) {
        case kOffChan890:   OnChannelWrite(value); return true;
        case kOffLatch8A0:  OnLatchWrite(value, 0u); return true;
        case kOffCtrl880:
        case kOffFormat888:
            {
                std::lock_guard<std::mutex> lk(mtx_);
                (off == kOffCtrl880 ? reg_880_ : reg_888_) = value;
            }
            RequireStreamFormat();
            return true;
        default: break;
    }
    std::lock_guard<std::mutex> lk(mtx_);
    switch (off) {
        case kOffStrobe898:
            /* loc_F618CC @0xF6196E sw $s1(=1); dword_F622D4 @0xF6238E sw $a3(=1)
               gated on 0x0884 bit0 @0xF62380; loc_F61EAC @0xF61EF8 lw / @0xF61EFA
               li $a2,2 / @0xF61EFC neg / @0xF61EFE and / @0xF61F00 sw. */
            if (value > 1u) {
                LOG(Caution, "EM-500 audio 0x0898 write 0x%08X outside the "
                             "loc_F618CC / dword_F622D4 / loc_F61EAC set\n", value);
                CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
            }
            reg_898_ = value;
            return true;
        case kOffEnable884: reg_884_ = value & ~kEnableBusyBit; return true;
        case kOffGate8C4:   reg_8C4_ = value; return true;
        case kOffAckL8C8:   reg_8C8_ = value; return true;
        case kOffAckR8CC:   reg_8CC_ = value; return true;
        default: return false;
    }
}

void CasioJx670Audio::OnLatchWrite(uint32_t value, uint32_t keep_mask) {
    bool     started = false;
    bool     drained = false;
    uint32_t chan    = 0;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        const bool was = Running();
        reg_8A0_ = (reg_8A0_ & keep_mask) | (value & ~keep_mask);
        started = !was && Running();
        drained = was && !Running() && queued_ == 0u && !capture_;
        chan    = reg_890_;
    }
    if (started && (chan & (kChanPlay | kChanCapture)) == 0u) {
        emu_->Get<Fatal>().Die("EM-500 audio: 0x08A0 start with 0x0890 = 0x%08X (playback not "
                               "enabled); not modeled", chan);
    }
    if (drained) speaker_->Finish();
    if (started) StartStream();
}

void CasioJx670Audio::OnChannelWrite(uint32_t value) {
    if ((value & kChanPlay) != 0u && (value & kChanCapture) != 0u) {
        emu_->Get<Fatal>().Die("EM-500 audio: 0x0890 write 0x%08X enables playback and capture "
                               "together", value);
    }
    bool     started      = false;
    bool     clears_play  = false;
    bool     latched_stop = false;
    uint32_t in_flight    = 0;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        const bool was  = Streaming();
        const bool stop = capture_ && CaptureEnabled() && (value & kChanCapture) == 0u &&
                          queued_ != 0u;
        in_flight    = queued_;
        clears_play  = PlayEnabled() && (value & kChanPlay) == 0u && in_flight != 0u;
        latched_stop = stop && Running();
        if (!clears_play && !latched_stop) {
            reg_890_ = value;
            if (stop) {
                clock_->Disarm(event_);
                queued_ = 0u;
            }
            started  = !was && Streaming() && Running();
        }
    }
    if (clears_play) {
        emu_->Get<Fatal>().Die("EM-500 audio: 0x0890 write 0x%08X clears playback enable with %u "
                               "blocks in flight; not modeled", value, in_flight);
    }
    if (latched_stop) {
        emu_->Get<Fatal>().Die("EM-500 audio: 0x0890 write 0x%08X clears capture enable with the "
                               "0x08A0 latch set and %u blocks in flight", value, in_flight);
    }
    if (started) StartStream();
}

void CasioJx670Audio::RequireStreamFormat() {
    uint32_t select = 0, stream_rate = 0, queued = 0;
    uint16_t channels = 0, stream_channels = 0;
    bool     doubler = false;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (!Running() && queued_ == 0u) return;
        select          = reg_880_ & kRateSelectMask;
        doubler         = rate_doubler_;
        channels        = Channels();
        stream_rate     = rate_hz_;
        stream_channels = channels_;
        queued          = queued_;
    }
    const uint32_t rate = ladder_->RequireRate(select, doubler);
    if (rate == stream_rate && channels == stream_channels) return;
    emu_->Get<Fatal>().Die("EM-500 audio: the %u Hz x %u ch stream changed to %u Hz x %u ch "
                           "with %u blocks in flight", stream_rate, stream_channels, rate,
                           channels, queued);
}

uint32_t CasioJx670Audio::FrameBytes() const {
    if (capture_) return CasioJx670Capture::kFrameBytes;
    return static_cast<uint32_t>(channels_) * (kBitsPerSample / 8u);
}

void CasioJx670Audio::StartStream() {
    uint32_t select    = 0;
    bool     doubler   = false;
    uint32_t in_flight = 0;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        select    = reg_880_ & kRateSelectMask;
        doubler   = rate_doubler_;
        in_flight = queued_;
    }
    if (in_flight != 0u) {
        emu_->Get<Fatal>().Die("EM-500 audio: stream restart with %u blocks in flight; not "
                               "modeled", in_flight);
    }
    const uint32_t rate = ladder_->RequireRate(select, doubler);
    uint16_t channels = 0;
    bool     capture = false;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        clock_->Disarm(event_);
        queued_      = 0u;
        ran_dry_     = false;
        rate_hz_     = rate;
        channels_    = Channels();
        capture_     = !PlayEnabled() && CaptureEnabled();
        frames_.SetRate(clock_->ClockRate(), GuestCycleClock::Rate{rate_hz_, 1u});
        frames_.Start(clock_->Cycles());
        channels = channels_;
        capture  = capture_;
    }
    if (capture) {
        mic_->Request(rate);
        mic_->Clear();
    } else {
        speaker_->SetFormat(rate, channels, kBitsPerSample);
        speaker_->Begin();
    }
    QueueDescriptor(kDescCurStart);
    QueueDescriptor(kDescNextStart);
}

void CasioJx670Audio::QueueDescriptor(uint32_t start_index) {
    uint32_t start_va = 0;
    uint32_t end_va = 0;
    uint32_t frame_bytes = 0;
    bool     capture = false;
    bool     dry = false;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (!Running() || !Streaming()) return;
        start_va = desc_[start_index];
        end_va = desc_[start_index + 1u];
        frame_bytes = FrameBytes();
        capture = capture_;
        dry = ran_dry_;
    }
    if (dry) {
        emu_->Get<Fatal>().Die("EM-500 audio: DMA descriptor 0x%08X..0x%08X queued after the "
                               "stream ran dry; not modeled", start_va, end_va);
    }
    const uint64_t span = static_cast<uint64_t>(end_va) -
                          static_cast<uint64_t>(start_va) + 1ull;
    if (end_va < start_va || span > PacedWaveOut::kMaxBlock) {
        LOG(Caution, "EM-500 audio DMA descriptor 0x%08X..0x%08X is not a block of "
                     "at most %u bytes\n", start_va, end_va, PacedWaveOut::kMaxBlock);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    const uint32_t length = static_cast<uint32_t>(span);
    if (length % frame_bytes != 0u) {
        emu_->Get<Fatal>().Die("EM-500 audio: DMA descriptor 0x%08X..0x%08X is not a whole "
                               "number of %u-byte frames", start_va, end_va, frame_bytes);
    }
    {
        std::lock_guard<std::mutex> lk(mtx_);
        const uint64_t start = queued_ != 0u ? blocks_[queued_ - 1u].end
                                             : frames_.TicksAt(clock_->Cycles());
        blocks_[queued_] = Block{start_va, length, start + length / frame_bytes};
        ++queued_;
        if (queued_ == 1u) ArmBlockEndLocked();
    }
    if (!capture) speaker_->QueueBlock(start_va, length);
}

void CasioJx670Audio::ArmBlockEndLocked() {
    clock_->Arm(event_, frames_.CycleOfTick(blocks_[0].end));
}

void CasioJx670Audio::OnRateChange() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (rate_hz_ == 0u) return;
    frames_.Rescale(clock_->Cycles(), clock_->ClockRate(), GuestCycleClock::Rate{rate_hz_, 1u});
    if (queued_ != 0u) ArmBlockEndLocked();
}

void CasioJx670Audio::RestartHostOutput() {
    Block    pending[kMaxQueued] = {};
    uint32_t count = 0, rate = 0, frame_bytes = 0;
    uint64_t frame = 0;
    uint16_t channels = 0;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        count = queued_;
        std::copy(blocks_, blocks_ + kMaxQueued, pending);
        frame       = frames_.TicksAt(clock_->Cycles());
        rate        = rate_hz_;
        channels    = channels_;
        frame_bytes = FrameBytes();
    }
    speaker_->SetFormat(rate, channels, kBitsPerSample);
    speaker_->Begin();
    if (count == 0u) return;
    const int64_t left_frames = static_cast<int64_t>(pending[0].end - frame);
    if (left_frames > 0) {
        const uint32_t left = static_cast<uint32_t>(left_frames) * frame_bytes;
        speaker_->QueueBlock(pending[0].va + pending[0].length - left, left);
    }
    for (uint32_t i = 1; i < count; ++i) speaker_->QueueBlock(pending[i].va, pending[i].length);
}

void CasioJx670Audio::OnBlockEnd() {
    bool  drained = false;
    Block captured;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (capture_) captured = blocks_[0];
        desc_[kDescCurStart] = desc_[kDescNextStart];
        desc_[kDescCurEnd] = desc_[kDescNextEnd];
        blocks_[0] = blocks_[1];
        --queued_;
        if (queued_ != 0u) ArmBlockEndLocked();
        else ran_dry_ = true;
        /* casio_cassiopeia_em500_ppc2000 wavedev.dll loc_F61EAC @0xF61EBC sw 0 -> 0x08A0;
           @0xF61EC8 lw 0x0884 / @0xF61ECC and 4 / @0xF61ECE beqz / @0xF61ED2 b. */
        drained = queued_ == 0u && !Running() && !capture_;
    }
    if (captured.length != 0u) mic_->WriteBlock(captured.va, captured.length);
    status_8A8_.fetch_or(kStatusBlockDone, std::memory_order_acq_rel);
    on_irq_change_();
    if (drained) speaker_->Finish();
}

void CasioJx670Audio::SaveState(StateWriter& w) const {
    std::lock_guard<std::mutex> lk(mtx_);
    w.Write("reg_880", reg_880_);
    w.Write("reg_884", reg_884_);
    w.Write("reg_888", reg_888_);
    w.Write("reg_890", reg_890_);
    w.Write("reg_898", reg_898_);
    w.Write("reg_8A0", reg_8A0_);
    for (uint32_t v : desc_) w.Write("desc", v);
    w.Write("reg_8C4", reg_8C4_);
    w.Write("reg_8C8", reg_8C8_);
    w.Write("reg_8CC", reg_8CC_);
    w.Write<uint8_t>("rate_doubler", rate_doubler_ ? 1u : 0u);
    w.Write("status_8A8", status_8A8_.load(std::memory_order_acquire));
    const uint64_t now       = clock_->Cycles();
    const bool     streaming = rate_hz_ != 0u;
    w.Write("stream_rate", rate_hz_);
    w.Write("stream_channels", channels_);
    w.Write<uint8_t>("stream_capture", capture_ ? 1u : 0u);
    w.Write("blocks_queued", queued_);
    w.Write<uint8_t>("stream_ran_dry", ran_dry_ ? 1u : 0u);
    for (const Block& b : blocks_) {
        w.Write("block_va", b.va);
        w.Write("block_length", b.length);
        w.Write("block_end", b.end);
    }
    const RatedTickCount::Position at =
        streaming ? frames_.PositionAt(now) : RatedTickCount::Position{};
    w.Write("frame", at.ticks);
    w.Write("frame_phase", at.phase);
    w.Write("frame_phase_den", at.phase_den);
}

void CasioJx670Audio::RestoreState(StateReader& r) {
    speaker_->Abort();
    mic_->Clear();
    std::lock_guard<std::mutex> lk(mtx_);
    r.Read("reg_880", reg_880_);
    r.Read("reg_884", reg_884_);
    r.Read("reg_888", reg_888_);
    r.Read("reg_890", reg_890_);
    r.Read("reg_898", reg_898_);
    r.Read("reg_8A0", reg_8A0_);
    for (uint32_t& v : desc_) r.Read("desc", v);
    r.Read("reg_8C4", reg_8C4_);
    r.Read("reg_8C8", reg_8C8_);
    r.Read("reg_8CC", reg_8CC_);
    uint8_t doubler = 0;
    r.Read("rate_doubler", doubler);
    rate_doubler_ = doubler != 0;
    uint16_t status = 0;
    r.Read("status_8A8", status);
    status_8A8_.store(status, std::memory_order_release);
    RatedTickCount::Position at;
    r.Read("stream_rate", rate_hz_);
    r.Read("stream_channels", channels_);
    uint8_t capture = 0;
    r.Read("stream_capture", capture);
    capture_ = capture != 0;
    r.Read("blocks_queued", queued_);
    uint8_t dry = 0;
    r.Read("stream_ran_dry", dry);
    ran_dry_ = dry != 0u;
    for (Block& b : blocks_) {
        r.Read("block_va", b.va);
        r.Read("block_length", b.length);
        r.Read("block_end", b.end);
    }
    r.Read("frame", at.ticks);
    r.Read("frame_phase", at.phase);
    r.Read("frame_phase_den", at.phase_den);

    clock_->Disarm(event_);
    if (rate_hz_ == 0u) return;
    frames_.SetRate(clock_->ClockRate(), GuestCycleClock::Rate{rate_hz_, 1u});
    frames_.PlaceAt(clock_->Cycles(), at);
    if (queued_ == 0u) return;
    ArmBlockEndLocked();
}

void CasioJx670Audio::PostRestore() {
    bool     capture = false;
    uint32_t rate    = 0;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (queued_ == 0u && !Running()) return;
        capture = capture_;
        rate    = rate_hz_;
    }
    if (capture) {
        mic_->Request(rate);
        return;
    }
    RestartHostOutput();
}
