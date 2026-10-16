#define NOMINMAX

#include "pxa2xx_dma.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/cpu/emulated_memory.h>
#include <cerfcore/state/state_stream.h>
#include "../irq_controller.h"
#include "pxa2xx_dma_port.h"

#include <algorithm>

namespace cerfcore {

void Pxa2xxDma::AttachChannels(uint32_t count) {
    channels_ = count;
    clock_    = &emu_.Get<GuestCycleClock>();
    mem_      = &emu_.Get<EmulatedMemory>();
    intc_     = &emu_.Get<IrqController>();
    for (uint32_t ch = 0; ch < count; ++ch) {
        bound_request_[ch] = kNoRequest;
        done_ev_[ch]       = clock_->Add([this, ch] { OnDoneEvent(ch); });
    }
    request_ev_ = clock_->Add([this] { OnRequestWake(); });
}

void Pxa2xxDma::DeferPortChange() { port_change_deferred_ = true; }

void Pxa2xxDma::SettleDeferredLocked() {
    while (port_change_deferred_) {
        port_change_deferred_ = false;
        PortChangeLocked();
    }
}

void Pxa2xxDma::DieUnservedRequest(uint32_t ch, uint32_t request) {
    emu_.Get<Fatal>().Die("Pxa2xxDma ch%u: DCMD 0x%08X waits for DMA request %u; not modelled", ch, dcmd_[ch],
                          request);
}

bool Pxa2xxDma::RequestMapsChannelLocked(uint32_t ch) const {
    uint32_t mapped = 0;
    return MapsChannel(bound_request_[ch], mapped) && mapped == ch;
}

/* Intel PXA255 Developer's Manual Table 5-8 (page 5-20) MAPVLD: "This bit can also be used to mask the
   request."; Figure 5-4 (page 5-8): a request moves the channel out of "Wait for request". */
void Pxa2xxDma::DrcmrWrittenLocked(uint32_t request) {
    const uint64_t now    = clock_->Cycles();
    uint32_t       target = 0;
    const bool     maps   = MapsChannel(request, target);
    for (uint32_t ch = 0; ch < channels_; ++ch) {
        if (bound_request_[ch] == request && !(maps && target == ch)) UnbindToWaitLocked(ch, now);
    }
    if (maps && (dcsr_[target] & RUN) != 0u && (PortBoundLocked(target) || UnboundWaitingLocked(target))) {
        SoleRequestOfLocked(target);
        if (!PortBoundLocked(target)) {
            if (PortOf(request) == nullptr) DieUnservedRequest(target, request);
            BindWaitingLocked(target, request);
        }
    }
    UpdateIrqLocked();
}

/* Intel PXA255 Developer's Manual Figure 5-4 (page 5-8): Stopped returns to "Valid descriptor not
   running"; only RUN=1 from that state enters "Descriptor fetch (running)". */
void Pxa2xxDma::RunEdgeLocked(uint32_t ch, bool was_run) {
    const bool now_run = (dcsr_[ch] & RUN) != 0u;
    if (!was_run && now_run) {
        if ((dcsr_[ch] & NODESCFETCH) == 0u && (ddadr_[ch] & DDADR_STOP) != 0u) {
            stopped_run_[ch] = true;
            return;
        }
        const uint32_t r = SoleRequestOfLocked(ch);
        if (r == kNoRequest || PortOf(r) == nullptr) {
            RunUnboundLocked(ch);
            return;
        }
        StartPortLocked(ch, r);
        return;
    }
    if (!was_run || now_run) return;
    if (stopped_run_[ch]) {
        stopped_run_[ch] = false;
        return;
    }
    if (moving_[ch]) {
        StopMoveLocked(ch);
    } else if (PortBoundLocked(ch)) {
        StopPortLocked(ch, clock_->Cycles(), false);
    } else {
        StopUnboundLocked(ch);
    }
}

void Pxa2xxDma::StartPortLocked(uint32_t ch, uint32_t request) {
    bound_request_[ch] = static_cast<uint8_t>(request);
    stream_[ch].Bind(PortOf(request));
    const uint64_t now = clock_->Cycles();
    if (DescriptorFetchModeLocked(ch)) {
        FetchDescriptorLocked(ch, now);
    } else {
        BeginServiceLocked(ch, now);
    }
    ArmDoneLocked(ch);
}

void Pxa2xxDma::BindWaitingLocked(uint32_t ch, uint32_t request) {
    ReleaseUnboundWaitLocked(ch);
    bound_request_[ch] = static_cast<uint8_t>(request);
    stream_[ch].Bind(PortOf(request));
    BeginServiceLocked(ch, clock_->Cycles());
    ArmDoneLocked(ch);
}

void Pxa2xxDma::UnbindToWaitLocked(uint32_t ch, uint64_t now) {
    if (!moving_[ch]) EvaluateLocked(ch, now);
    if (!PortBoundLocked(ch)) return;
    const bool waiting = moving_[ch];
    ReleasePortLocked(ch, now);
    if (!waiting) WaitUnboundLocked(ch);
}

/* Intel PXA255 Developer's Manual section 5.1.4.2 (page 5-7): continue or stop "as determined by the
   DDADR[STOP] bit"; section 5.2.4 (page 5-16): the channel "fetches the four-word descriptor". */
void Pxa2xxDma::FetchDescriptorLocked(uint32_t ch, uint64_t at) {
    if ((ddadr_[ch] & DDADR_STOP) != 0u) {
        StopPortLocked(ch, at, true);
        return;
    }
    LoadDescriptorLocked(ch, at);
}

bool Pxa2xxDma::AdmitDescriptorLoadLocked(uint32_t ch, uint64_t at) {
    if (load_at_[ch] != at) {
        load_at_[ch] = at;
        loads_[ch]   = 0u;
    }
    if (loads_[ch] >= kMaxDescriptors) {
        LOG(Caution, "Pxa2xxDma: ch%u chain exceeded %u descriptors (last DSADR=0x%08X DTADR=0x%08X "
            "DCMD=0x%08X DDADR=0x%08X) - stopping\n", ch, kMaxDescriptors, dsadr_[ch], dtadr_[ch],
            dcmd_[ch], ddadr_[ch]);
        return false;
    }
    ++loads_[ch];
    return true;
}

/* Intel PXA255 Developer's Manual section 5.1.4.2 (page 5-6): "In Descriptor Fetch Mode, the DMAC
   registers are loaded from DMA descriptors in main memory"; "The four-word DMA descriptor, aligned on a
   16-byte boundary in main memory, loads the following registers". */
bool Pxa2xxDma::ReadDescriptorLocked(uint32_t ch) {
    uint32_t w[4] = {};
    if (!ReadDescriptorWordsLocked(DescriptorAddressLocked(ch), w)) return false;
    block_handed_[ch] = false;
    ddadr_[ch] = w[0] & DescriptorDdadrMask();
    dsadr_[ch] = w[1];
    dtadr_[ch] = w[2];
    dcmd_[ch]  = w[3] & DescriptorDcmdMask();
    if ((dcmd_[ch] & STARTIRQEN) != 0u) dcsr_[ch] |= STARTINTR;
    return true;
}

void Pxa2xxDma::LoadDescriptorLocked(uint32_t ch, uint64_t at) {
    if (!AdmitDescriptorLoadLocked(ch, at)) {
        StopPortLocked(ch, at, false);
        return;
    }
    if (!ReadDescriptorLocked(ch)) {
        dcsr_[ch] |= BUSERRINTR;
        StopPortLocked(ch, at, false);
        return;
    }
    BeginServiceLocked(ch, at);
}

/* Intel PXA255 Developer's Manual Table 5-12 (page 5-24): FLOWSRC / FLOWTRG "1 - Wait for a request
   signal before initiating the data transfer"; INCSRCADDR / INCTRGADDR "If the source [target] address
   is an internal peripheral's FIFO address ... this bit must be 0"; (page 5-25) WIDTH. */
bool Pxa2xxDma::PortDescriptorLocked(uint32_t ch) const {
    const Pxa2xxDmaPort* port = stream_[ch].Port();
    const uint32_t       d    = dcmd_[ch];
    const bool           rx   = port->Receive();
    return port->BurstFifo() && (d & (FLOWSRC | FLOWTRG)) == (rx ? FLOWSRC : FLOWTRG) &&
           (d & (INCSRCADDR | INCTRGADDR)) == (rx ? INCTRGADDR : INCSRCADDR) &&
           (rx ? dsadr_[ch] : dtadr_[ch]) == port->FifoAddress() &&
           ((d >> kWidthShift) & kFieldMask) == kWidthWord;
}

/* Intel PXA255 Developer's Manual Table 5-12 (page 5-25) SIZE: "If DCMDx[LENGTH] is less than
   DCMDx[SIZE] the data transfer size equals DCMDx[LENGTH]". */
void Pxa2xxDma::BeginServiceLocked(uint32_t ch, uint64_t at) {
    Pxa2xxDmaChannel& s     = stream_[ch];
    Pxa2xxDmaPort*    port  = s.Port();
    if (!PortDescriptorLocked(ch)) {
        moving_[ch] = true;
        port->SetService(at, 1u, 0u);
        BeginMoveLocked(ch, at);
        return;
    }
    const uint32_t    bytes = dcmd_[ch] & kDcmdLengthMask;
    const uint32_t    burst = 1u << ((dcmd_[ch] >> kSizeShift) & kFieldMask);
    if (bytes != 0u && !mem_->CanCopyRange(port->Receive() ? dtadr_[ch] : dsadr_[ch], bytes, port->Receive())) {
        dcsr_[ch] |= BUSERRINTR;
        StopPortLocked(ch, at, false);
        return;
    }
    s.Begin((bytes + kWordBytes - 1u) / kWordBytes, burst);
    if (!port->Receive() && bytes != 0u) {
        port->TransmitBlock(dsadr_[ch], bytes, block_handed_[ch]);
        block_handed_[ch] = true;
    }
    port->SetService(at, burst, s.Remaining());
}

void Pxa2xxDma::WriteTransferLocked(uint32_t (&reg)[kMaxChannels], uint32_t ch, uint32_t value) {
    if (reg[ch] != value) block_handed_[ch] = false;
    reg[ch] = value;
}

void Pxa2xxDma::EvaluateLocked(uint32_t ch, uint64_t now) {
    Pxa2xxDmaChannel& s = stream_[ch];
    while (PortBoundLocked(ch)) {
        uint64_t done = 0;
        if (!s.Active() || !s.DoneCycle(done) || done > now) return;
        s.Port()->Settle(done);
        CompleteLocked(ch, done);
    }
}

/* Intel PXA255 Developer's Manual Table 5-7 (page 5-19) ENDINTR: "interrupt caused because the
   current transaction was successfully completed and DCMD[LENGTH] = 0". */
void Pxa2xxDma::CompleteLocked(uint32_t ch, uint64_t at) {
    Pxa2xxDmaChannel& s     = stream_[ch];
    const uint32_t    bytes = dcmd_[ch] & kDcmdLengthMask;
    if (s.Port()->Receive()) WriteReceivedLocked(ch);
    if ((dcmd_[ch] & INCSRCADDR) != 0u) dsadr_[ch] += bytes;
    if ((dcmd_[ch] & INCTRGADDR) != 0u) dtadr_[ch] += bytes;
    dcmd_[ch] &= ~kDcmdLengthMask;
    block_handed_[ch] = false;
    s.Finish();
    if ((dcmd_[ch] & ENDIRQEN) != 0u) dcsr_[ch] |= ENDINTR;
    const GuestCycleClock::Rate rate = clock_->ClockRate();
    LOG(SocDma, "ch%u done request %u length %u at cycle %llu rate %llu/%llu\n", ch, bound_request_[ch],
        bytes, static_cast<unsigned long long>(at), static_cast<unsigned long long>(rate.num),
        static_cast<unsigned long long>(rate.den));
    if (DescriptorFetchModeLocked(ch)) {
        FetchDescriptorLocked(ch, at);
    } else {
        StopPortLocked(ch, at, true);
    }
}

void Pxa2xxDma::EndMoveLocked(uint32_t ch, uint64_t at, MoveEnd end) {
    moving_[ch] = false;
    if (end == MoveEnd::BusError) dcsr_[ch] |= BUSERRINTR;
    if (end == MoveEnd::Done) block_handed_[ch] = false;
    if (end == MoveEnd::Done && DescriptorFetchModeLocked(ch)) {
        FetchDescriptorLocked(ch, at);
    } else {
        StopPortLocked(ch, at, end == MoveEnd::Done);
    }
    ArmDoneLocked(ch);
}

/* Intel PXA255 Developer's Manual Table 5-7 (page 5-18) RUN: "If the run bit is cleared in the
   middle of the burst, the burst will complete before the channel is stopped." */
void Pxa2xxDma::StopPortLocked(uint32_t ch, uint64_t at, bool end_of_chain) {
    Pxa2xxDmaPort* port = stream_[ch].Port();
    ReleasePortLocked(ch, at);
    dcsr_[ch] &= ~RUN;
    port->Stopped(at, end_of_chain);
}

void Pxa2xxDma::CommitPortLocked(uint32_t ch, uint64_t at) {
    Pxa2xxDmaChannel& s    = stream_[ch];
    Pxa2xxDmaPort*    port = s.Port();
    moving_[ch] = false;
    port->Settle(at);
    if (!s.Active()) return;
    if (port->Receive()) WriteReceivedLocked(ch);
    const uint32_t moved = MovedBytesLocked(ch);
    if ((dcmd_[ch] & INCSRCADDR) != 0u) dsadr_[ch] += moved;
    if ((dcmd_[ch] & INCTRGADDR) != 0u) dtadr_[ch] += moved;
    dcmd_[ch] -= moved;
    s.Stop();
}

void Pxa2xxDma::ReleasePortLocked(uint32_t ch, uint64_t at) {
    Pxa2xxDmaChannel& s = stream_[ch];
    CommitPortLocked(ch, at);
    s.Port()->SetService(at, std::max(s.Burst(), 1u), 0u);
    s.Unbind();
    bound_request_[ch] = kNoRequest;
    clock_->Disarm(done_ev_[ch]);
}

uint32_t Pxa2xxDma::MovedBytesLocked(uint32_t ch) const {
    return std::min(stream_[ch].WordsMoved() * kWordBytes, dcmd_[ch] & kDcmdLengthMask);
}

void Pxa2xxDma::WriteReceivedLocked(uint32_t ch) {
    Pxa2xxDmaChannel& s     = stream_[ch];
    const uint32_t    start = s.Written() * kWordBytes;
    const uint32_t    end   = MovedBytesLocked(ch);
    if (end <= start) return;
    s.Port()->ReceiveBlock(dtadr_[ch] + start, end - start);
    s.MarkWritten();
}

void Pxa2xxDma::ArmDoneLocked(uint32_t ch) {
    uint64_t at = 0;
    if (stream_[ch].DoneCycle(at)) {
        clock_->Arm(done_ev_[ch], at);
    } else {
        clock_->Disarm(done_ev_[ch]);
    }
}

void Pxa2xxDma::OnDoneEvent(uint32_t ch) {
    Locked lk(*this);
    EvaluateLocked(ch, clock_->Cycles());
    ArmDoneLocked(ch);
    UpdateIrqLocked();
}

void Pxa2xxDma::OnPortChange() {
    Locked lk(*this);
    PortChangeLocked();
}

void Pxa2xxDma::PortChangeLocked() {
    const uint64_t now = clock_->Cycles();
    for (uint32_t ch = 0; ch < channels_; ++ch) {
        if (!PortBoundLocked(ch)) continue;
        if (moving_[ch]) {
            RequestChangedLocked(ch, now);
            continue;
        }
        EvaluateLocked(ch, now);
        Pxa2xxDmaChannel& s = stream_[ch];
        if (s.Active() && !s.Port()->BurstFifo()) {
            SwitchToMovesLocked(ch, now);
        } else if (s.Active()) {
            s.Port()->SetService(now, s.Burst(), s.Remaining());
        }
        ArmDoneLocked(ch);
    }
    UpdateIrqLocked();
}

void Pxa2xxDma::SwitchToMovesLocked(uint32_t ch, uint64_t now) {
    CommitPortLocked(ch, now);
    moving_[ch] = true;
    stream_[ch].Port()->SetService(now, 1u, 0u);
    BeginMoveLocked(ch, now);
}

/* Intel PXA255 Developer's Manual section 5.1.2.1 (page 5-3): PREQ is level sensitive and "The DCSR[REQPEND]
   bit indicates the status of the pending request for the channel"; Figure 5-4 (page 5-8): a request moves
   the channel only from "Wait for request". */
bool Pxa2xxDma::RequestPendingLocked(uint32_t ch) {
    if (!PortBoundLocked(ch) || !RequestMapsChannelLocked(ch)) return false;
    Pxa2xxDmaPort* port = stream_[ch].Port();
    port->Settle(clock_->Cycles());
    return port->RequestAsserted();
}

/* Intel PXA255 Developer's Manual section 5.2.1.1 (page 5-12): "DSADRx is increased by the smaller
   value of DCMDx[LENGTH] and DCMD[SIZE]. DCMDx[LENGTH] is decreased by the same value"; section
   5.3.5 (page 5-21): DSADRx "are read only in the Descriptor Fetch Mode". */
uint32_t Pxa2xxDma::ChannelRegLocked(uint32_t ch, Reg reg) {
    Pxa2xxDmaChannel& s = stream_[ch];
    if (!s.Active()) {
        switch (reg) {
        case Reg::Ddadr: return ddadr_[ch];
        case Reg::Dsadr: return dsadr_[ch];
        case Reg::Dtadr: return dtadr_[ch];
        default:         return dcmd_[ch];
        }
    }
    s.Port()->Settle(clock_->Cycles());
    const uint32_t moved = MovedBytesLocked(ch);
    switch (reg) {
    case Reg::Ddadr: return ddadr_[ch];
    case Reg::Dsadr: return dsadr_[ch] + ((dcmd_[ch] & INCSRCADDR) != 0u ? moved : 0u);
    case Reg::Dtadr:
        if (s.Port()->Receive()) WriteReceivedLocked(ch);
        return dtadr_[ch] + ((dcmd_[ch] & INCTRGADDR) != 0u ? moved : 0u);
    default:
        return dcmd_[ch] - moved;
    }
}

void Pxa2xxDma::SampleRequestsLocked() {
    const uint64_t now  = clock_->Cycles();
    uint64_t       next = WaitingMoveRiseLocked(now);
    RequestAfterStopLocked(now, next);
    if (next == Pxa2xxDmaPort::kNoRise) {
        clock_->Disarm(request_ev_);
    } else {
        clock_->Arm(request_ev_, next);
    }
}

void Pxa2xxDma::OnRequestWake() {
    Locked lk(*this);
    const uint64_t now = clock_->Cycles();
    for (uint32_t ch = 0; ch < channels_; ++ch) {
        if (moving_[ch]) RequestChangedLocked(ch, now);
    }
    UpdateIrqLocked();
}

void Pxa2xxDma::UpdateIrqLocked() {
    SampleRequestsLocked();
    for (uint32_t ch = 0; ch < channels_; ++ch) {
        if (!ChannelIrq(ch)) continue;
        intc_->AssertIrq(static_cast<int>(kIntcDmaBit));
        return;
    }
    intc_->DeAssertIrq(static_cast<int>(kIntcDmaBit));
}

/* Intel PXA255 Developer's Manual Table 5-7 (page 5-18) and Table 5-12 (page 5-24): DCSR and DCMD
   reset 0 apart from STOPSTATE; Table 5-9 (page 5-21): DDADR STOP resets 0, the address is
   uninitialized. */
void Pxa2xxDma::ResetChannelsLocked() {
    const uint64_t now = clock_->Cycles();
    for (uint32_t ch = 0; ch < channels_; ++ch) {
        if (PortBoundLocked(ch)) StopPortLocked(ch, now, false);
        dcsr_[ch] = 0u;
        dcmd_[ch] = 0u;
        ddadr_[ch] &= ~0x3u;
        stopped_run_[ch]  = false;
        loads_[ch]        = 0u;
        block_handed_[ch] = false;
    }
    ResetUnboundLocked();
    UpdateIrqLocked();
}

void Pxa2xxDma::SaveChannels(StateWriter& w) {
    w.WriteBytes("dcsr", dcsr_, sizeof(dcsr_));
    w.WriteBytes("ddadr", ddadr_, sizeof(ddadr_));
    w.WriteBytes("dsadr", dsadr_, sizeof(dsadr_));
    w.WriteBytes("dtadr", dtadr_, sizeof(dtadr_));
    w.WriteBytes("dcmd", dcmd_, sizeof(dcmd_));
    w.WriteBytes("bound_request", bound_request_, sizeof(bound_request_));
    w.WriteBytes("desc_load_at", load_at_, sizeof(load_at_));
    w.WriteBytes("desc_loads", loads_, sizeof(loads_));
    for (uint32_t ch = 0; ch < kMaxChannels; ++ch) {
        w.Write<uint8_t>("stopped_run", stopped_run_[ch] ? 1u : 0u);
        w.Write<uint8_t>("moving", moving_[ch] ? 1u : 0u);
        stream_[ch].Save(w);
    }
    SaveUnbound(w);
}

void Pxa2xxDma::RestoreChannels(StateReader& r) {
    r.ReadBytes("dcsr", dcsr_, sizeof(dcsr_));
    r.ReadBytes("ddadr", ddadr_, sizeof(ddadr_));
    r.ReadBytes("dsadr", dsadr_, sizeof(dsadr_));
    r.ReadBytes("dtadr", dtadr_, sizeof(dtadr_));
    r.ReadBytes("dcmd", dcmd_, sizeof(dcmd_));
    r.ReadBytes("bound_request", bound_request_, sizeof(bound_request_));
    r.ReadBytes("desc_load_at", load_at_, sizeof(load_at_));
    r.ReadBytes("desc_loads", loads_, sizeof(loads_));
    for (uint32_t ch = 0; ch < kMaxChannels; ++ch) {
        uint8_t stopped_run = 0, moving = 0;
        r.Read("stopped_run", stopped_run);
        r.Read("moving", moving);
        stopped_run_[ch]  = stopped_run != 0u;
        moving_[ch]       = moving != 0u;
        block_handed_[ch] = false;
        const uint8_t request = bound_request_[ch];
        stream_[ch].Bind(request == kNoRequest ? nullptr : PortOf(request));
        stream_[ch].Restore(r);
    }
    RestoreUnbound(r);
}

void Pxa2xxDma::PostRestoreChannelsLocked() {
    for (uint32_t ch = 0; ch < channels_; ++ch) ArmDoneLocked(ch);
    PortChangeLocked();
}

}
