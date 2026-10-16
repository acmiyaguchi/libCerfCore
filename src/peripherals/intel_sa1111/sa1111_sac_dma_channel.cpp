#define NOMINMAX

#include "sa1111_sac_dma_channel.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/state/state_stream.h>
#include "sa1111_sac_request_lines.h"
#include "sa1111_system_controller.h"

#include <algorithm>

Sa1111SacDmaChannel::Sa1111SacDmaChannel(CerfEmulator& emu, const Names& names)
    : Service(emu), ctl_(names.keys), names_(names) {}

void Sa1111SacDmaChannel::OnReady() {
    clock_   = &emu_.Get<GuestCycleClock>();
    sysc_    = &emu_.Get<Sa1111SystemController>();
    lines_   = &emu_.Get<Sa1111SacRequestLines>();
    done_ev_ = clock_->Add([this] { Evaluate(clock_->Cycles()); });
    emu_.Get<Sa1111Sbi>().RegisterGrantListener([this] { OnGrantChange(); });
    sysc_->RegisterClockListener([this] { OnGrantChange(); });
    OnChannelReady();
}

void Sa1111SacDmaChannel::SetFifoControlReader(std::function<Sa1111SacDmaControl::Fifo()> read) {
    fifo_ = std::move(read);
}

void Sa1111SacDmaChannel::SetChangeListener(std::function<void(uint64_t now)> on_change) {
    on_change_ = std::move(on_change);
}

uint32_t Sa1111SacDmaChannel::ReadRegister(uint32_t index) {
    if (index == 0u) Evaluate(clock_->Cycles());
    return ctl_.Read(index);
}

void Sa1111SacDmaChannel::WriteRegister(uint32_t index, uint32_t value) {
    if (index == 0u) WriteControl(value);
    else             ctl_.WriteBuffer(index, value);
}

void Sa1111SacDmaChannel::WriteControl(uint32_t value) {
    const uint64_t now = clock_->Cycles();
    Evaluate(now);
    ctl_.WriteControl(value);
#if CERF_DEV_MODE
    LOG(Periph, "[Sa1111Sac] %s W 0x%08X -> 0x%08X (running=%d buf=%c)\n", names_.control, value,
        ctl_.Read(0u), running_ ? 1 : 0, buffer_b_ ? 'B' : 'A');
#endif
    if (!ctl_.Enabled()) {
        Stop();
        OnStopped();
        return;
    }
    if (!running_) {
        ApplyGrant(now, sysc_->DmaGrant());
        TryStartNext(now, false);
    }
    Evaluate(now);
}

/* SA-1111 Developer's Manual §7.2.2 (printed 7-5): "When the complete block is transferred, it
   sets the appropriate DMA_Done bit in the status register." */
void Sa1111SacDmaChannel::CatchUp(uint64_t now) {
    for (;;) {
        if (running_ && words_left_ != 0u && fifo_().enabled) FillTo(now);
        if (!running_ || words_left_ != 0u || ctl_.InterruptEnabled() || end_cycle_ > now) break;
        CompleteBlock(end_cycle_);
    }
    SettleTo(now);
}

void Sa1111SacDmaChannel::Evaluate(uint64_t now) {
    CatchUp(now);
    if (running_ && ctl_.InterruptEnabled() && words_left_ == 0u && end_cycle_ <= now) {
        CompleteBlock(now);
        CatchUp(now);
    }
    ArmDone(now);
    on_change_(now);
}

void Sa1111SacDmaChannel::ArmDone(uint64_t now) {
    uint64_t cycle = end_cycle_;
    if (running_ && ctl_.InterruptEnabled() && (words_left_ == 0u || PlannedEnd(cycle))) {
        clock_->Arm(done_ev_, std::max(now, cycle));
    } else {
        clock_->Disarm(done_ev_);
    }
}

void Sa1111SacDmaChannel::OnGrantChange() {
    const Sa1111Sbi::BusGrant grant = sysc_->DmaGrant();
    if (grant == CurrentGrant()) return;
    const uint64_t now = clock_->Cycles();
    Evaluate(now);
    ApplyGrant(now, grant);
    Evaluate(now);
}

void Sa1111SacDmaChannel::TryStartNext(uint64_t at, bool prefer_b) {
    bool buffer_b = false;
    if (ctl_.NextBuffer(prefer_b, buffer_b)) StartBlock(at, buffer_b);
}

void Sa1111SacDmaChannel::StartBlock(uint64_t at, bool buffer_b) {
    running_    = true;
    buffer_b_   = buffer_b;
    ctl_.Begin(buffer_b);
    words_left_ = ctl_.Bytes(buffer_b) / 4u;
    end_cycle_  = at;
    OnBlockStart(at, buffer_b);
#if CERF_DEV_MODE
    LOG(Periph, "[Sa1111Sac] %s start buf=%c pa=0x%08X bytes=%u %s=0x%08X cyc=%llu\n", names_.tag,
        buffer_b ? 'B' : 'A', ctl_.Address(buffer_b), ctl_.Bytes(buffer_b), names_.control,
        ctl_.Read(0u), static_cast<unsigned long long>(at));
#endif
}

void Sa1111SacDmaChannel::CompleteBlock(uint64_t at) {
    const bool buffer_b = buffer_b_;
    running_ = false;
    const bool raise = ctl_.Complete(buffer_b, KeepStarted(buffer_b));
#if CERF_DEV_MODE
    LOG(Periph, "[Sa1111Sac] %s done buf=%c %s=0x%08X cyc=%llu last=%llu\n", names_.tag,
        buffer_b ? 'B' : 'A', names_.control, ctl_.Read(0u), static_cast<unsigned long long>(at),
        static_cast<unsigned long long>(end_cycle_));
#endif
    OnBlockDone();
    if (raise) lines_->StrobeDone(buffer_b ? names_.done_b : names_.done_a);
    if (ctl_.Enabled()) TryStartNext(at, !buffer_b);
    else                ctl_.Idle();
}

void Sa1111SacDmaChannel::Stop() {
    running_    = false;
    words_left_ = 0u;
    clock_->Disarm(done_ev_);
}

void Sa1111SacDmaChannel::Reset(uint64_t now) {
    Stop();
    ctl_.Reset();
    OnReset(now);
}

void Sa1111SacDmaChannel::Save(StateWriter& w) const {
    ctl_.Save(w);
    w.Write<uint32_t>(names_.running, running_ ? 1u : 0u);
    w.Write<uint32_t>(names_.buffer_b, buffer_b_ ? 1u : 0u);
    w.Write<uint32_t>(names_.words_left, words_left_);
    SaveExtra(w);
}

void Sa1111SacDmaChannel::Restore(StateReader& r, uint64_t now) {
    ctl_.Restore(r);
    uint32_t running = 0u, buffer_b = 0u;
    r.Read(names_.running, running);
    r.Read(names_.buffer_b, buffer_b);
    r.Read(names_.words_left, words_left_);
    running_   = running != 0u;
    buffer_b_  = buffer_b != 0u;
    end_cycle_ = now;
    clock_->Disarm(done_ev_);
    RestoreExtra(r, now);
}

void Sa1111SacDmaChannel::PostRestore() {
    const uint64_t now = clock_->Cycles();
    ApplyGrant(now, sysc_->DmaGrant());
    OnRestored();
    ArmDone(now);
}
