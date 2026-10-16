#define NOMINMAX

#include "pxa2xx_ac97_out_fifo.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/cpu/emulated_memory.h>
#include "../../execution/guest_cycle_clock.h"
#include "../../peripherals/ac97_codec.h"
#include <cerfcore/state/state_stream.h>
#include "../pxa255/pxa255_id.h"
#include "../pxa27x/pxa270_id.h"
#include "pxa2xx_ac97_interrupt.h"
#include "pxa2xx_dma.h"

#include <algorithm>
#include <string>

namespace cerfcore {

namespace {

/* Intel PXA255 Developer's Manual section 13.8.1 (page 13-18): transmit FIFOs with "sixteen 32-bit entries";
   "A transmit FIFO triggers a DMA request when it holds less than eight entries. A transmit FIFO must be
   half full (filled with eight entries) before any data is transmitted across the AC-link." */
constexpr uint32_t kFifoDepth   = 16u;
constexpr uint32_t kTxThreshold = 7u;
constexpr uint32_t kHalfFull    = 8u;

}  // namespace

bool Pxa2xxAc97OutFifo::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && (bd->GetSocId() == SocId::Pxa255 || bd->GetSocId() == SocId::Pxa270);
}

void Pxa2xxAc97OutFifo::OnReady() {
    link_  = &emu_.Get<Pxa2xxAc97Link>();
    codec_ = emu_.TryGet<Ac97Codec>();
    clock_ = &emu_.Get<GuestCycleClock>();
    mem_   = &emu_.Get<EmulatedMemory>();
    dma_   = &emu_.Get<Pxa2xxDma>();
    irq_   = &emu_.Get<Pxa2xxAc97Interrupt>();
    link_->AddListener(this);
    tx_.Configure(kFifoDepth, kTxThreshold, kHalfFull);
    if (codec_ != nullptr) dma_->RegisterPort(Request(), this);
}

void Pxa2xxAc97OutFifo::Settle(uint64_t now) {
    link_->Settle(now);
    SettleTo(now);
}

/* Intel PXA255 Developer's Manual section 13.8.1.1 (page 13-18): "During transmit under-run conditions, the
   last valid sample is continuously sent out across the AC-link." */
void Pxa2xxAc97OutFifo::SettleTo(uint64_t now) {
    if (!link_->Running() || !primed_) return;
    const uint64_t started = link_->FrameIndexAt(now) + 1u;
    const uint64_t from    = std::max(out_frames_, prime_frame_);
    if (started <= from) return;
    const uint64_t takes = law_.Count(started) - law_.Count(from);
    if (tx_.Take(takes) != 0u) out_error_ = true;
    out_frames_ = started;
    law_.Prune(started);
}

/* Intel PXA255 Developer's Manual section 13.6.1 (page 13-15): "The ACUNIT continues to transmit zeroes
   until the transmit FIFO is half full. When it is half full, valid transmit FIFO data is sent across the
   AC-link." */
void Pxa2xxAc97OutFifo::CheckPrimed(uint64_t now) {
    if (primed_ || tx_.Level() < kHalfFull) return;
    primed_      = true;
    prime_frame_ = link_->Running() ? link_->FrameIndexAt(now) + 1u : 0u;
    LOG(SocIis, "AC'97 %s out primed from frame %llu (link %s)\n", KeyPrefix(),
        static_cast<unsigned long long>(prime_frame_), link_->Running() ? "running" : "stopped");
}

void Pxa2xxAc97OutFifo::OnLinkRun(uint64_t) {
    out_frames_  = 0u;
    prime_frame_ = 0u;
    law_.Reset(0u, SlotPace());
}

void Pxa2xxAc97OutFifo::OnLinkStop(uint64_t cycle) { SettleTo(cycle); }

/* Intel PXA27x Developer's Manual section 13.6.2 (page 13-17): ACOFF "discards all remaining data
   in the transmit FIFO and receive FIFO"; Table 13-8 (page 13-22) nCRST: "All data in the
   controller and the Codec is lost." */
void Pxa2xxAc97OutFifo::OnFifoReset(uint64_t cycle, bool cold) {
    SettleTo(cycle);
    tx_.Clear();
    tx_.SetSupply(0u);
    primed_ = false;
    if (cold) out_error_ = false;
}

void Pxa2xxAc97OutFifo::OnCodecWrite(uint64_t cycle) {
    SettleTo(cycle);
    const uint64_t       next = link_->FrameIndexAt(cycle) + 1u;
    const Pxa2xxSlotPace pace = SlotPace();
    if (pace != law_.PaceAt(next)) {
        LOG(SocIis, "AC'97 %s out %u Hz / %u from frame %llu\n", KeyPrefix(), pace.rate, pace.div,
            static_cast<unsigned long long>(next));
    }
    law_.Change(next, pace);
}

void Pxa2xxAc97OutFifo::OnLinkEvent() { dma_->OnPortChange(); }

bool Pxa2xxAc97OutFifo::ServiceRequest() const { return link_->RequestsEnabled() && tx_.Level() <= tx_.Threshold(); }

bool Pxa2xxAc97OutFifo::RequestAsserted() const { return link_->DmaRequestsEnabled() && ServiceRequest(); }

uint64_t Pxa2xxAc97OutFifo::NextRequestCycle(uint64_t now) {
    if (!link_->DmaRequestsEnabled()) return kNoRise;
    return NextStatusCycle(now, kFsr);
}

/* Intel PXA27x Developer's Manual Table 13-21 (page 13-37) MOSR: FIFOE "Set when a FIFO error occurs",
   FSR "1 = FIFO needs servicing". */
uint32_t Pxa2xxAc97OutFifo::Status(uint64_t now) {
    Settle(now);
    uint32_t s = out_error_ ? kFifoe : 0u;
    if (ServiceRequest()) s |= kFsr;
    return s;
}

void Pxa2xxAc97OutFifo::ClearStatus(uint64_t now, uint32_t mask) {
    Settle(now);
    if ((mask & kFifoe) != 0u) out_error_ = false;
}

void Pxa2xxAc97OutFifo::WriteData(uint64_t now, uint32_t value, bool overrun_is_error) {
    link_->RequireCodec(KeyPrefix(), "write");
    Settle(now);
    if (tx_.Level() >= kFifoDepth) {
        if (overrun_is_error) out_error_ = true;
        return;
    }
    tx_.Put();
    CheckPrimed(now);
    OnWord(value);
}

void Pxa2xxAc97OutFifo::SetService(uint64_t now, uint32_t burst_words, uint64_t supply_words) {
    Settle(now);
    const uint64_t supply = link_->DmaRequestsEnabled() ? supply_words : 0u;
    tx_.Configure(kFifoDepth, kTxThreshold, burst_words);
    tx_.SetSupply(supply);
    tx_.Refill();
    CheckPrimed(now);
    irq_->Kick();
}

bool Pxa2xxAc97OutFifo::TakeCycle(uint64_t n, uint64_t& cycle) const {
    if (!link_->Running() || !primed_) return false;
    const uint64_t from  = std::max(out_frames_, prime_frame_);
    uint64_t       frame = 0;
    if (!law_.FrameOfSlot(law_.Count(from) + n, frame)) return false;
    cycle = link_->CycleOfFrame(frame);
    return true;
}

bool Pxa2xxAc97OutFifo::CycleOfMoved(uint64_t words, uint64_t& cycle) {
    uint64_t n = 0;
    if (!tx_.TakesToMove(words, n)) return false;
    if (n == 0u) {
        cycle = clock_->Cycles();
        return true;
    }
    return TakeCycle(n, cycle);
}

/* Intel PXA27x Developer's Manual Table 13-9 (page 13-24) POINT: an interrupt when "POCR[FEIE] is 0b1 and
   POSR[FIFOE] is 0b1" or "POCR[FSRIE] is 0b1 and POSR[FSR] is 0b1". */
uint64_t Pxa2xxAc97OutFifo::NextStatusCycle(uint64_t now, uint32_t watched) {
    Settle(now);
    uint64_t       at   = Pxa2xxAc97Link::kNever;
    const uint64_t left = tx_.TakesBeforeEmpty();
    if (left == DmaBurstFifo::kUnlimited) return at;
    uint64_t c = 0;
    if ((watched & kFifoe) != 0u && !out_error_ && TakeCycle(left + 1u, c)) at = c;
    if ((watched & kFsr) != 0u && link_->RequestsEnabled() && !ServiceRequest() &&
        TakeCycle(left - kTxThreshold, c) && c < at) {
        at = c;
    }
    return at;
}

void Pxa2xxAc97OutFifo::TransmitBlock(uint32_t pa, uint32_t bytes, bool resumed) {
    if (!resumed) OnBlock(pa, bytes);
}

void Pxa2xxAc97OutFifo::ReceiveBlock(uint32_t pa, uint32_t bytes) {
    emu_.Get<Fatal>().Die("Pxa2xxAc97OutFifo: receive block of %u bytes at 0x%08X on the %s out FIFO", bytes,
                          pa, KeyPrefix());
}

void Pxa2xxAc97OutFifo::Stopped(uint64_t now, bool) {
    Settle(now);
    OnDmaStopped();
    irq_->Kick();
}

void Pxa2xxAc97OutFifo::Save(StateWriter& w) {
    Settle(clock_->Cycles());
    const std::string p = KeyPrefix();
    w.Write<uint32_t>((p + "_tx_level").c_str(), tx_.Level());
    w.Write<uint64_t>((p + "_tx_moved").c_str(), tx_.Moved());
    w.Write<uint8_t>((p + "_primed").c_str(), primed_ ? 1u : 0u);
    w.Write<uint64_t>((p + "_prime_frame").c_str(), prime_frame_);
    w.Write<uint64_t>((p + "_out_frames").c_str(), out_frames_);
    w.Write<uint8_t>((p + "_out_error").c_str(), out_error_ ? 1u : 0u);
    law_.Save(w, (p + "_out").c_str());
}

void Pxa2xxAc97OutFifo::Restore(StateReader& r) {
    const std::string p = KeyPrefix();
    uint32_t tx_level = 0;
    uint64_t tx_moved = 0;
    uint8_t  primed = 0, out_error = 0;
    r.Read((p + "_tx_level").c_str(), tx_level);
    r.Read((p + "_tx_moved").c_str(), tx_moved);
    r.Read((p + "_primed").c_str(), primed);
    r.Read((p + "_prime_frame").c_str(), prime_frame_);
    r.Read((p + "_out_frames").c_str(), out_frames_);
    r.Read((p + "_out_error").c_str(), out_error);
    law_.Restore(r, (p + "_out").c_str());
    tx_.Restore(tx_level, tx_moved);
    tx_.SetSupply(0u);
    primed_    = primed != 0u;
    out_error_ = out_error != 0u;
    OnRestored();
}

}
