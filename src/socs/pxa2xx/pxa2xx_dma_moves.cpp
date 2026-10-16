#define NOMINMAX

#include "pxa2xx_dma_moves.h"

#include "pxa2xx_dma_port.h"

#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/cpu/emulated_memory.h>
#include "../../cpu/physical_bus.h"
#include <cerfcore/state/state_stream.h>

#include <algorithm>

/* Intel PXA255 Developer's Manual Table 5-12 (page 5-25) WIDTH: "01 - 1 byte", "10 - HalfWord (2
   bytes)", "11 - Word (4 Bytes)"; SIZE: "01 - 8 Bytes", "10 - 16 Bytes", "11 - 32 Bytes". */
uint32_t Pxa2xxDmaMoves::UnitWidth(uint32_t dcmd) {
    switch ((dcmd >> 14) & 3u) {
    case 1u: return 1u;
    case 2u: return 2u;
    default: return 4u;
    }
}

uint32_t Pxa2xxDmaMoves::SizeBytes(uint32_t dcmd) {
    return 4u << ((dcmd >> 16) & 3u);
}

void Pxa2xxDmaMoves::OnPortChange() {
    if (access_thread_.load(std::memory_order_relaxed) == std::this_thread::get_id()) {
        DeferPortChange();
        return;
    }
    Pxa2xxDma::OnPortChange();
}

bool Pxa2xxDmaMoves::BusAccess(uint32_t pa, uint32_t width, bool write, uint32_t& value) {
    if (MoveBusErrorLocked(pa, write)) return false;
    access_thread_.store(std::this_thread::get_id(), std::memory_order_relaxed);
    auto&           bus  = emu_.Get<PhysicalBus>();
    const MmioWidth unit = static_cast<MmioWidth>(width);
    const bool      ok   = write ? bus.Write(pa, unit, value) : bus.Read(pa, unit, &value);
    access_thread_.store(std::thread::id{}, std::memory_order_relaxed);
    return ok;
}

bool Pxa2xxDmaMoves::ReadUnit(uint32_t pa, uint32_t width, uint32_t& value) {
    value = 0u;
    return BusAccess(pa, width, false, value);
}

bool Pxa2xxDmaMoves::WriteUnit(uint32_t pa, uint32_t width, uint32_t value) {
    return BusAccess(pa, width, true, value);
}

void Pxa2xxDmaMoves::WaitUnboundLocked(uint32_t ch) {
    phase_[ch] = Phase::Idle;
    DescriptorLoadedLocked(ch);
    DriveLocked(ch);
}

void Pxa2xxDmaMoves::ReleaseUnboundWaitLocked(uint32_t ch) { phase_[ch] = Phase::Idle; }

bool Pxa2xxDmaMoves::ReadDescriptorWordsLocked(uint32_t pa, uint32_t (&w)[4]) {
    if (DescriptorInMemoryOnly()) {
        const uint8_t* src = emu_.Get<EmulatedMemory>().TryTranslateRange(pa, sizeof(w));
        if (src == nullptr) return false;
        for (uint32_t i = 0; i < 4u; ++i) w[i] = cerf::le::U32(src, 4u * i);
        return true;
    }
    for (uint32_t i = 0; i < 4u; ++i) {
        if (!ReadUnit(pa + 4u * i, 4u, w[i])) return false;
    }
    return true;
}

void Pxa2xxDmaMoves::DriveLocked(uint32_t ch) {
    if (driving_[ch]) return;
    driving_[ch] = true;
    while (Moving(phase_[ch])) StepLocked(ch);
    driving_[ch] = false;
}

void Pxa2xxDmaMoves::StepLocked(uint32_t ch) {
    switch (phase_[ch]) {
    case Phase::Fetch:   LoadFetchedLocked(ch);    return;
    case Phase::Read:    ReadChunkLocked(ch);      return;
    case Phase::Write:   WriteChunkLocked(ch);     return;
    case Phase::Compare: CompareLocked(ch);        return;
    case Phase::Done:    DescriptorDoneLocked(ch); return;
    case Phase::Discard: DiscardLocked(ch);        return;
    default:             return;
    }
}

void Pxa2xxDmaMoves::RunUnboundLocked(uint32_t ch) {
    if (phase_[ch] != Phase::Idle) return;
    if (DescriptorFetchModeLocked(ch)) {
        BeginFetchLocked(ch);
    } else {
        DescriptorLoadedLocked(ch);
    }
    DriveLocked(ch);
}

void Pxa2xxDmaMoves::StopUnboundLocked(uint32_t ch) { FinishLocked(ch); }

void Pxa2xxDmaMoves::BeginMoveLocked(uint32_t ch, uint64_t) {
    DescriptorLoadedLocked(ch);
    DriveLocked(ch);
}

void Pxa2xxDmaMoves::StopMoveLocked(uint32_t ch) {
    StopUnboundLocked(ch);
}

void Pxa2xxDmaMoves::RequestChangedLocked(uint32_t ch, uint64_t) {
    if (phase_[ch] != Phase::Wait || !RequestPendingLocked(ch)) return;
    StartTransferLocked(ch);
    DriveLocked(ch);
}

uint64_t Pxa2xxDmaMoves::WaitingMoveRiseLocked(uint64_t now) {
    uint64_t next = Pxa2xxDmaPort::kNoRise;
    for (uint32_t ch = 0; ch < channels_; ++ch) {
        if (phase_[ch] != Phase::Wait || !PortBoundLocked(ch)) continue;
        Pxa2xxDmaPort* port = BoundPortLocked(ch);
        port->Settle(now);
        const uint64_t rise = port->RequestAsserted() ? now : port->NextRequestCycle(now);
        if (rise < next) next = rise;
    }
    return next;
}

void Pxa2xxDmaMoves::BeginFetchLocked(uint32_t ch) {
    if ((ddadr_[ch] & DDADR_STOP) != 0u || !AdmitDescriptorLoadLocked(ch, clock_->Cycles())) {
        FinishLocked(ch);
        return;
    }
    phase_[ch] = Phase::Fetch;
}

void Pxa2xxDmaMoves::LoadFetchedLocked(uint32_t ch) {
    if (!ReadDescriptorLocked(ch)) {
        BusErrorLocked(ch);
        return;
    }
    DescriptorLoadedLocked(ch);
}

void Pxa2xxDmaMoves::DescriptorLoadedLocked(uint32_t ch) {
    if (AwaitRequestLocked(ch)) return;
    StartTransferLocked(ch);
}

/* Intel PXA255 Developer's Manual Table 5-12 (page 5-24) FLOWSRC / FLOWTRG: "If either the
   DCMD[FLOWSRC] or DCMD[FLOWTRG] bit is set, the current DMA does not initiate a transfer until it
   receives a request"; section 5.2.4 (page 5-16): memory-to-memory moves need neither. */
bool Pxa2xxDmaMoves::AwaitRequestLocked(uint32_t ch) {
    const uint32_t dcmd = dcmd_[ch];
    if ((dcmd & (FLOWSRC | FLOWTRG)) == 0u) return false;
    uint32_t request = 0;
    if (PortBoundLocked(ch)) {
        if (RequestPendingLocked(ch)) return false;
    } else if (AnyRequestMapsLocked(ch, request)) {
        DieUnservedRequest(ch, request);
    } else {
        LOG(SocDma, "ch%u waits for a request on DCMD 0x%08X with no request mapped to the channel\n", ch, dcmd);
    }
    phase_[ch] = Phase::Wait;
    return true;
}

void Pxa2xxDmaMoves::StartTransferLocked(uint32_t ch) {
    const uint32_t dcmd = dcmd_[ch];
    if (CompareEnabled(dcmd)) {
        phase_[ch] = Phase::Compare;
        return;
    }
    if ((dcmd & kDcmdLengthMask) != 0u) {
        BeginChunkLocked(ch);
        return;
    }
    phase_[ch] = DiscardsEmptyDescriptor() ? Phase::Discard : Phase::Done;
}

void Pxa2xxDmaMoves::DiscardLocked(uint32_t ch) {
    if (PortBoundLocked(ch) && DescriptorFetchModeLocked(ch)) {
        EndLocked(ch, MoveEnd::Done);
        return;
    }
    if (DescriptorFetchModeLocked(ch)) {
        BeginFetchLocked(ch);
        return;
    }
    LOG(Caution, "Pxa2xxDma: ch%u no-descriptor-fetch with DCMD[LEN]=0 (DCMD=0x%08X) - stopping\n", ch,
        dcmd_[ch]);
    FinishLocked(ch);
}

/* Intel PXA255 Developer's Manual section 5.2.4 (page 5-16): "The DMAC sends a request to the memory
   controller to read the number of bytes addressed by DSADRx[31:0] into a 32-byte staging buffer";
   "The smaller value of DCMDx[SIZE] or DCMDx[LENGTH] specifies the number of bytes". */
void Pxa2xxDmaMoves::BeginChunkLocked(uint32_t ch) {
    chunk_[ch] = std::min(SizeBytes(dcmd_[ch]), dcmd_[ch] & kDcmdLengthMask);
    phase_[ch] = Phase::Read;
}

void Pxa2xxDmaMoves::ReadChunkLocked(uint32_t ch) {
    const uint32_t width = UnitWidth(dcmd_[ch]);
    const uint32_t src   = MoveAddressLocked(ch, dsadr_[ch]);
    const bool     inc   = (dcmd_[ch] & INCSRCADDR) != 0u;
    for (uint32_t off = 0; off < chunk_[ch]; off += width) {
        uint32_t value = 0;
        if (!ReadUnit(inc ? src + off : src, width, value)) {
            BusErrorLocked(ch);
            return;
        }
        cerf::le::PutN(&staged_[ch][off], value, width);
    }
    phase_[ch] = Phase::Write;
}

/* Intel PXA255 Developer's Manual section 5.2.4 (page 5-16): "The DMAC generates a write cycle to the
   location addressed in DTADRx[31:0]"; "DSADRx and DTADRx are increased by the smaller value of
   DCMD[SIZE] and DCMDx[LENGTH]". */
void Pxa2xxDmaMoves::WriteChunkLocked(uint32_t ch) {
    const uint32_t width = UnitWidth(dcmd_[ch]);
    const uint32_t dst   = MoveAddressLocked(ch, dtadr_[ch]);
    const bool     inc   = (dcmd_[ch] & INCTRGADDR) != 0u;
    for (uint32_t off = 0; off < chunk_[ch]; off += width) {
        const uint32_t value = static_cast<uint32_t>(cerf::le::UN(&staged_[ch][off], width));
        if (!WriteUnit(inc ? dst + off : dst, width, value)) {
            BusErrorLocked(ch);
            return;
        }
    }
    if ((dcmd_[ch] & INCSRCADDR) != 0u) dsadr_[ch] += chunk_[ch];
    if ((dcmd_[ch] & INCTRGADDR) != 0u) dtadr_[ch] += chunk_[ch];
    dcmd_[ch] -= chunk_[ch];
    if ((dcmd_[ch] & kDcmdLengthMask) != 0u) {
        if (AwaitRequestLocked(ch)) return;
        BeginChunkLocked(ch);
        return;
    }
    DescriptorDoneLocked(ch);
}

void Pxa2xxDmaMoves::CompareLocked(uint32_t ch) {
    const uint32_t width = UnitWidth(dcmd_[ch]);
    uint32_t       src = 0, trg = dtadr_[ch];
    if (!ReadUnit(MoveAddressLocked(ch, dsadr_[ch]), width, src) ||
        (!CompareImmediate(dcmd_[ch]) && !ReadUnit(MoveAddressLocked(ch, dtadr_[ch]), width, trg))) {
        BusErrorLocked(ch);
        return;
    }
    SetCompareStatusLocked(ch, src == trg);
    if (PortBoundLocked(ch)) {
        EndLocked(ch, MoveEnd::Done);
        return;
    }
    if (!DescriptorFetchModeLocked(ch)) {
        FinishLocked(ch);
        return;
    }
    BeginFetchLocked(ch);
}

/* Intel PXA255 Developer's Manual Table 5-12 (page 5-24) ENDIRQEN: "set DCSR[EndIntr] interrupt for
   the channel when DCMD[LENGTH] is decreased to zero". */
void Pxa2xxDmaMoves::DescriptorDoneLocked(uint32_t ch) {
    if ((dcmd_[ch] & ENDIRQEN) != 0u) dcsr_[ch] |= ENDINTR;
    if (PortBoundLocked(ch)) {
        EndLocked(ch, MoveEnd::Done);
        return;
    }
    if (DescriptorFetchModeLocked(ch)) {
        BeginFetchLocked(ch);
        return;
    }
    FinishLocked(ch);
}

void Pxa2xxDmaMoves::BusErrorLocked(uint32_t ch) {
    EndLocked(ch, MoveEnd::BusError);
}

void Pxa2xxDmaMoves::FinishLocked(uint32_t ch) {
    EndLocked(ch, MoveEnd::Stopped);
}

void Pxa2xxDmaMoves::EndLocked(uint32_t ch, MoveEnd end) {
    phase_[ch] = Phase::Idle;
    if (PortBoundLocked(ch)) {
        EndMoveLocked(ch, clock_->Cycles(), end);
        return;
    }
    if (end == MoveEnd::BusError) dcsr_[ch] |= BUSERRINTR;
    dcsr_[ch] &= ~RUN;
}

void Pxa2xxDmaMoves::ResetUnboundLocked() {
    for (uint32_t ch = 0; ch < channels_; ++ch) phase_[ch] = Phase::Idle;
}

void Pxa2xxDmaMoves::SaveUnbound(StateWriter& w) {
    for (uint32_t ch = 0; ch < kMaxChannels; ++ch) {
        w.Write<uint8_t>("move_phase", static_cast<uint8_t>(phase_[ch]));
    }
}

void Pxa2xxDmaMoves::RestoreUnbound(StateReader& r) {
    for (uint32_t ch = 0; ch < kMaxChannels; ++ch) {
        uint8_t phase = 0;
        r.Read("move_phase", phase);
        phase_[ch] = static_cast<Phase>(phase);
    }
}
