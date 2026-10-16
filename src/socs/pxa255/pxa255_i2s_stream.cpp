#include "pxa255_i2s_stream.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/cpu/emulated_memory.h>
#include <cerfcore/state/state_stream.h>
#include "../pxa2xx/pxa2xx_dma.h"
#include "pxa255_i2s_bit_clock.h"
#include "pxa255_i2s_fifo_reset_sync.h"
#include "pxa255_i2s_fifo_values.h"
#include "pxa255_i2s_host_output.h"
#include "pxa255_i2s_interrupt.h"
#include "pxa255_id.h"

namespace {

/* Intel PXA255 Developer's Manual Table 14-3 (page 14-9) SACR0: reset 0x7700, BCKD 2, RST 3, EFWR 4, STRF 5;
   Table 14-6 (page 14-11) SACR1: DREC 3, DRPL 4, ENLBF 5; Table 14-7 (page 14-12): SASR0 reset 0x1. */
constexpr uint32_t kSacr0Reset = 0x7700u;
constexpr uint32_t kSacr0Bckd = 1u << 2, kSacr0Rst = 1u << 3, kSacr0Efwr = 1u << 4, kSacr0Strf = 1u << 5;
constexpr uint32_t kSacr1Drec = 1u << 3, kSacr1Drpl = 1u << 4, kSacr1Enlbf = 1u << 5;
constexpr uint32_t kSacr1Defined = 0x39u;
constexpr uint32_t kSasr0Reset   = 0x1u;
/* Intel PXA255 Developer's Manual section 14.2 (page 14-2): SYNC is BITCLK divided by 64. */
constexpr uint64_t kBitsPerFrame = 64u;
/* Intel PXA255 Developer's Manual section 14.6.1 (page 14-8): "It takes four BITCLK cycles and four
   internal clock cycles before SACR0[ENB] is conveyed to the slower BITCLK domain"; section 14.6.2
   (page 14-10) says the same of DRPL, DREC and AMSL. */
constexpr uint64_t kSyncBits = 4u;
/* Intel PXA255 Developer's Manual Table 5-5 (page 5-13): I2S FIFO 0x4040_0080, receive on DRCMR
   0x4000_0108, transmit on DRCMR 0x4000_010c; section 14.5.1 (page 14-6): "FIFO buffers are 16
   levels deep and 32 bits wide". */
constexpr uint32_t kSadrPa = 0x40400080u;
constexpr uint32_t kRequestRx = 2u, kRequestTx = 3u;
constexpr uint32_t kFifoDepth = 16u;

uint64_t FramesStarted(uint64_t bits) { return bits < kSyncBits ? 0u : (bits - kSyncBits) / kBitsPerFrame + 1u; }
uint64_t FramesEnded(uint64_t bits) { return bits < kSyncBits ? 0u : (bits - kSyncBits) / kBitsPerFrame; }
uint64_t FrameStartBit(uint64_t frame) { return kSyncBits + frame * kBitsPerFrame; }

}  // namespace

bool Pxa255I2sStream::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Pxa255;
}

void Pxa255I2sStream::OnReady() {
    clock_     = &emu_.Get<GuestCycleClock>();
    mem_       = &emu_.Get<EmulatedMemory>();
    dma_       = &emu_.Get<Pxa2xxDma>();
    irq_       = &emu_.Get<Pxa255I2sInterrupt>();
    bit_clock_ = &emu_.Get<Pxa255I2sBitClock>();
    values_    = &emu_.Get<Pxa255I2sFifoValues>();
    resets_    = &emu_.Get<Pxa255I2sFifoResetSync>();
    host_      = &emu_.Get<Pxa255I2sHostOutput>();
    irq_->Attach([this] { return enabled_; }, [this](uint64_t now) { return Sasr0(now); },
                 [this](uint64_t now, uint32_t up, uint32_t down) { return NextStatusChange(now, up, down); });
    bit_clock_->Attach([this] { return Pxa255I2sBitClock::Inputs{enabled_, (sacr0_ & kSacr0Bckd) != 0u}; },
                       [this](uint64_t now) { Settle(now); },
                       [this](uint64_t now, bool was_running) { OnBitClock(now, was_running); });
    dma_->RegisterPort(kRequestTx, &tx_port_);
    dma_->RegisterPort(kRequestRx, &rx_port_);
    sacr0_ = kSacr0Reset;
    tx_.Configure(kFifoDepth, TxThreshold(), 1u);
    rx_.Configure(kFifoDepth, RxFreeThreshold(), 1u);
    rx_.Restore(kFifoDepth, 0u);
}

void Pxa255I2sStream::ConfigureThresholds() {
    tx_.Configure(kFifoDepth, TxThreshold(), tx_.Burst());
    rx_.Configure(kFifoDepth, RxFreeThreshold(), rx_.Burst());
}

uint32_t Pxa255I2sStream::Replay() const { return (sacr1_ & kSacr1Drpl) == 0u ? 1u : 0u; }
uint32_t Pxa255I2sStream::Record() const { return (sacr1_ & kSacr1Drec) == 0u ? 1u : 0u; }
bool     Pxa255I2sStream::Running() const { return bit_clock_->Running(); }

/* Intel PXA255 Developer's Manual Table 14-3 (page 14-9) RST: "Resets FIFOs logic and all registers, except
   this register (SACR0)", "1 = Reset is active to other registers". */
bool Pxa255I2sStream::InReset() const { return (sacr0_ & kSacr0Rst) != 0u; }

/* Intel PXA255 Developer's Manual Table 14-4 (page 14-10): EFWR 1 STRF 0 "I2SLINK cannot read the Transmit
   FIFO but can write to the Receive FIFO"; EFWR 1 STRF 1 "I2SLINK can read the Transmit FIFO but cannot write
   to the Receive FIFO"; with EFWR 1 the CPU or DMA writes and reads the FIFO that STRF selects. */
bool Pxa255I2sStream::LinkReadsTx() const { return (sacr0_ & kSacr0Efwr) == 0u || (sacr0_ & kSacr0Strf) != 0u; }
bool Pxa255I2sStream::LinkWritesRx() const { return (sacr0_ & kSacr0Efwr) == 0u || (sacr0_ & kSacr0Strf) == 0u; }

bool Pxa255I2sStream::PioOnTx(bool write) const {
    if ((sacr0_ & kSacr0Efwr) == 0u) return write;
    return (sacr0_ & kSacr0Strf) == 0u;
}

/* Intel PXA255 Developer's Manual Table 14-7 (page 14-12) SASR0: RFL 15:12, TFL 11:8, RFS 4 "Receive FIFO
   level is at or above RFL threshold", TFS 3 "Transmit FIFO level is at or below TFL threshold", BSY 2,
   RNE 1, TNF 0; RFS and TFS read 0 with the I2S disabled. */
uint32_t Pxa255I2sStream::Sasr0(uint64_t now) {
    Settle(now);
    if (InReset()) return kSasr0Reset;
    const uint32_t tx_level = tx_.Level();
    const uint32_t rx_words = kFifoDepth - rx_.Level();
    uint32_t v = ((rx_words & 0xFu) << 12) | ((tx_level & 0xFu) << 8);
    if (enabled_ && rx_words >= Rfth() + 1u) v |= 1u << 4;
    if (enabled_ && tx_level <= TxThreshold()) v |= 1u << 3;
    if (Running()) v |= 1u << 2;
    if (rx_words != 0u) v |= 1u << 1;
    if (tx_level < kFifoDepth) v |= 1u << 0;
    return v;
}

/* Intel PXA255 Developer's Manual Table 14-4 (page 14-10), EFWR 0: "I2SLINK reads from the Transmit
   FIFO and writes to the Receive FIFO"; Table 14-7 (page 14-12) TUR "I2S attempted data read from an
   empty Transmit FIFO", ROR "I2S attempted data write to full Receive FIFO". */
void Pxa255I2sStream::Settle(uint64_t now) {
    if (!Running()) return;
    const uint64_t bits    = bit_clock_->BitsAt(now);
    const uint64_t started = FramesStarted(bits);
    const uint64_t ended   = FramesEnded(bits);
    uint64_t       frame   = 0;
    if (resets_->TxPending(frame) && frame < started) {
        TakeTo(frame);
        tx_.Clear();
        values_->TxClear();
        resets_->TxDone();
    }
    TakeTo(started);
    if (resets_->RxPending(frame) && frame <= ended) {
        PushTo(frame);
        ClearRx();
        resets_->RxDone();
    }
    PushTo(ended);
}

void Pxa255I2sStream::ClearRx() {
    rx_.Restore(kFifoDepth, rx_.Moved());
    values_->RxClear();
}

/* Intel PXA255 Developer's Manual Table 14-6 (page 14-11) ENLBF: "I2S/MSB Interface Loop Back Function is
   enabled". */
void Pxa255I2sStream::TakeTo(uint64_t frames) {
    if (frames <= out_frames_) return;
    const uint64_t first = out_law_.Count(out_frames_);
    const uint64_t n     = out_law_.Count(frames) - first;
    if (n != 0u && !InReset() && LinkReadsTx()) {
        const uint64_t         before = tx_.Moved();
        DmaBurstFifo::Arrivals arrivals;
        const uint64_t         shortfall = tx_.Take(n, &arrivals);
        if (shortfall != 0u) irq_->NoteUnderrun();
        const bool            loop = (sacr1_ & kSacr1Enlbf) != 0u;
        std::vector<uint32_t> departed;
        values_->TxTake(tx_, arrivals, before, n - shortfall, loop ? &departed : nullptr);
        if (loop) values_->LoopRecord(out_law_, first, n, departed);
    }
    out_frames_ = frames;
    out_law_.Prune(frames);
}

void Pxa255I2sStream::PushTo(uint64_t frames) {
    if (frames <= in_frames_) return;
    const uint64_t first = in_law_.Count(in_frames_);
    const uint64_t m     = in_law_.Count(frames) - first;
    if (m != 0u && !InReset() && LinkWritesRx()) {
        std::vector<uint32_t> looped;
        if ((sacr1_ & kSacr1Enlbf) != 0u) values_->LoopTake(in_law_, first, m, looped);
        if (PushWords(m, looped.empty() ? nullptr : &looped) != 0u) irq_->NoteOverrun();
    }
    values_->LoopDrop(frames);
    in_frames_ = frames;
    in_law_.Prune(frames);
}

uint64_t Pxa255I2sStream::PushWords(uint64_t m, const std::vector<uint32_t>* values) {
    DmaBurstFifo::Arrivals arrivals;
    const uint64_t         shortfall = rx_.Take(m, &arrivals);
    values_->RxPush(rx_, arrivals, m - shortfall, values);
    return shortfall;
}

bool Pxa255I2sStream::TakeCycle(uint64_t n, uint64_t& cycle) const {
    uint64_t frame = 0;
    if (!Running() || !out_law_.FrameOfSlot(out_law_.Count(out_frames_) + n, frame)) return false;
    cycle = bit_clock_->CycleOfBit(FrameStartBit(frame));
    return true;
}

bool Pxa255I2sStream::PushCycle(uint64_t m, uint64_t& cycle) const {
    uint64_t frame = 0;
    if (!Running() || !in_law_.FrameOfSlot(in_law_.Count(in_frames_) + m, frame)) return false;
    cycle = bit_clock_->CycleOfBit(FrameStartBit(frame + 1u));
    return true;
}

bool Pxa255I2sStream::TxCycleOfMoved(uint64_t words, uint64_t& cycle) {
    uint64_t n = 0;
    if (!tx_.TakesToMove(words, n)) return false;
    if (n == 0u) {
        cycle = clock_->Cycles();
        return true;
    }
    return TakeCycle(n, cycle);
}

bool Pxa255I2sStream::RxCycleOfMoved(uint64_t words, uint64_t& cycle) {
    uint64_t m = 0;
    if (!rx_.TakesToMove(words, m)) return false;
    if (m == 0u) {
        cycle = clock_->Cycles();
        return true;
    }
    return PushCycle(m, cycle);
}

bool Pxa255I2sStream::TxRise(uint64_t& cycle) const {
    const uint64_t left = tx_.TakesBeforeEmpty();
    if (!LinkReadsTx() || left == DmaBurstFifo::kUnlimited || left <= TxThreshold()) return false;
    return TakeCycle(left - TxThreshold(), cycle);
}

bool Pxa255I2sStream::RxRise(uint64_t& cycle) const {
    const uint64_t left = rx_.TakesBeforeEmpty();
    if (!LinkWritesRx() || left == DmaBurstFifo::kUnlimited || left <= RxFreeThreshold()) return false;
    return PushCycle(left - RxFreeThreshold(), cycle);
}

uint64_t Pxa255I2sStream::NextStatusChange(uint64_t now, uint32_t rising, uint32_t falling) {
    Settle(now);
    uint64_t at = Pxa255I2sInterrupt::kNever;
    if (!Running() || InReset()) return at;
    const auto keep = [&at](uint64_t cycle) {
        if (cycle < at) at = cycle;
    };
    const uint32_t tx_moves = rising & (Pxa255I2sInterrupt::kTfs | Pxa255I2sInterrupt::kTur);
    const uint32_t rx_moves = (rising & (Pxa255I2sInterrupt::kRfs | Pxa255I2sInterrupt::kRor)) |
                              (falling & Pxa255I2sInterrupt::kRfs);
    uint64_t       c       = 0;
    if (tx_moves != 0u && resets_->TxPending(c)) keep(bit_clock_->CycleOfBit(FrameStartBit(c)));
    if (rx_moves != 0u && resets_->RxPending(c)) keep(bit_clock_->CycleOfBit(FrameStartBit(c)));
    const uint64_t tx_left = tx_.TakesBeforeEmpty();
    const uint64_t rx_left = rx_.TakesBeforeEmpty();
    if ((rising & Pxa255I2sInterrupt::kTfs) != 0u && TxRise(c)) keep(c);
    if ((rising & Pxa255I2sInterrupt::kTur) != 0u && LinkReadsTx() && tx_left != DmaBurstFifo::kUnlimited &&
        TakeCycle(tx_left + 1u, c)) {
        keep(c);
    }
    if ((rising & Pxa255I2sInterrupt::kRfs) != 0u && RxRise(c)) keep(c);
    if ((rising & Pxa255I2sInterrupt::kRor) != 0u && LinkWritesRx() && rx_left != DmaBurstFifo::kUnlimited &&
        PushCycle(rx_left + 1u, c)) {
        keep(c);
    }
    return at;
}

uint32_t Pxa255I2sStream::Port::FifoAddress() const { return kSadrPa; }

bool Pxa255I2sStream::Port::BurstFifo() const { return (i2s_.sacr0_ & kSacr0Efwr) == 0u; }

/* Intel PXA255 Developer's Manual section 14.3.2 (page 14-4) DRPL: "Transmit DMA requests are
   disabled"; section 14.3.3 (page 14-5) DREC: "Receive DMA requests are disabled". */
bool Pxa255I2sStream::Port::DmaOff() const {
    return !i2s_.enabled_ || i2s_.InReset() || (i2s_.sacr1_ & (receive_ ? kSacr1Drec : kSacr1Drpl)) != 0u;
}

bool Pxa255I2sStream::Port::RequestAsserted() const {
    const DmaBurstFifo& fifo = receive_ ? i2s_.rx_ : i2s_.tx_;
    return !DmaOff() && fifo.Level() <= fifo.Threshold();
}

uint64_t Pxa255I2sStream::Port::NextRequestCycle(uint64_t) {
    uint64_t cycle = 0;
    if (DmaOff() || !(receive_ ? i2s_.RxRise(cycle) : i2s_.TxRise(cycle))) return kNoRise;
    return cycle;
}

void Pxa255I2sStream::Port::SetService(uint64_t now, uint32_t burst_words, uint64_t supply_words) {
    i2s_.Settle(now);
    const uint64_t supply = DmaOff() ? 0u : supply_words;
    DmaBurstFifo& fifo = receive_ ? i2s_.rx_ : i2s_.tx_;
    fifo.Configure(kFifoDepth, receive_ ? i2s_.RxFreeThreshold() : i2s_.TxThreshold(), burst_words);
    fifo.SetSupply(supply);
    const uint64_t         before = fifo.Moved();
    DmaBurstFifo::Arrivals arrivals;
    fifo.Refill(&arrivals);
    if (receive_) {
        i2s_.values_->RxPush(fifo, arrivals, 0u, nullptr);
    } else {
        i2s_.values_->TxTake(fifo, arrivals, before, 0u, nullptr);
    }
    i2s_.irq_->Refresh();
}

void Pxa255I2sStream::Port::TransmitBlock(uint32_t pa, uint32_t bytes, bool resumed) {
    i2s_.block_.resize(bytes);
    i2s_.mem_->CopyOut(pa, i2s_.block_.data(), bytes);
    i2s_.values_->TxLoadBlock(i2s_.block_.data(), bytes, i2s_.tx_.Moved());
    if (!resumed) i2s_.host_->Queue(i2s_.block_.data(), bytes, i2s_.bit_clock_->Rate());
}

void Pxa255I2sStream::Port::ReceiveBlock(uint32_t pa, uint32_t bytes) {
    i2s_.block_.resize(bytes);
    i2s_.values_->RxWriteBlock(i2s_.block_.data(), bytes);
    i2s_.mem_->CopyIn(pa, i2s_.block_.data(), bytes);
}

void Pxa255I2sStream::Port::Stopped(uint64_t now, bool) {
    i2s_.Settle(now);
    i2s_.irq_->Refresh();
    if (!receive_) i2s_.host_->Finish();
}

/* Intel PXA255 Developer's Manual section 14.6.1 (page 14-8): "Setting ENB to one ... enables DMA
   requests"; Table 14-3 (page 14-9): "Transmit DMA Request asserted whenever the Transmit FIFO has <
   (TFTH+1) entries", "Receive DMA Request asserted whenever the Receive FIFO has >= (RFTH+1) entries". */
void Pxa255I2sStream::Enable(uint64_t now, uint32_t sacr0) {
    sacr0_       = sacr0;
    enabled_     = true;
    gated_sacr1_ = sacr1_;
    out_frames_  = 0u;
    in_frames_   = 0u;
    ConfigureThresholds();
    values_->LoopClear();
    values_->ClearLastSent();
    out_law_.Reset(0u, Pxa2xxSlotPace{Replay(), 1u});
    in_law_.Reset(0u, Pxa2xxSlotPace{Record(), 1u});
    bit_clock_->Restart(now);
    dma_->OnPortChange();
    irq_->Refresh();
}

void Pxa255I2sStream::ChangeSacr0(uint64_t now, uint32_t sacr0) {
    Settle(now);
    sacr0_ = sacr0;
    ConfigureThresholds();
    bit_clock_->Update(now);
    dma_->OnPortChange();
    irq_->Refresh();
}

/* Intel PXA255 Developer's Manual section 14.6.1 (page 14-8): clearing ENB "resets the Receive FIFO",
   "does not affect the Transmit FIFO" and "de-asserts all DMA requests". */
void Pxa255I2sStream::Disable(uint64_t now, uint32_t sacr0) {
    Settle(now);
    sacr0_   = sacr0;
    enabled_ = false;
    ConfigureThresholds();
    resets_->Cancel();
    ClearRx();
    bit_clock_->Restart(now);
    dma_->OnPortChange();
    irq_->Refresh();
}

void Pxa255I2sStream::ResetHeld(uint64_t now) {
    tx_.Clear();
    values_->TxClear();
    values_->ClearLastSent();
    ClearRx();
    const uint32_t old = sacr1_;
    sacr1_       = 0u;
    gated_sacr1_ = 0u;
    resets_->Cancel();
    if (Running()) ApplySacr1(now, old);
    irq_->ResetRegisters();
    bit_clock_->ResetSadiv(now);
    dma_->OnPortChange();
    irq_->Refresh();
}

void Pxa255I2sStream::OnBitClock(uint64_t now, bool was_running) {
    if (was_running && !Running()) gated_sacr1_ = sacr1_;
    if (!was_running && Running()) ApplySacr1(now, gated_sacr1_);
    dma_->OnPortChange();
    irq_->Refresh();
}

void Pxa255I2sStream::WriteSacr1(uint64_t now, uint32_t value) {
    Settle(now);
    const uint32_t old = sacr1_;
    sacr1_ = value & kSacr1Defined;
    if (Running()) ApplySacr1(now, old);
    dma_->OnPortChange();
    irq_->Refresh();
}

void Pxa255I2sStream::ApplySacr1(uint64_t now, uint32_t old) {
    const uint64_t bits  = bit_clock_->BitsAt(now);
    const uint64_t first = (bits + kBitsPerFrame - 1u) / kBitsPerFrame;
    out_law_.Change(first, Pxa2xxSlotPace{Replay(), 1u});
    in_law_.Change(first, Pxa2xxSlotPace{Record(), 1u});
    resets_->Apply(sacr1_, old, bits, first);
}

void Pxa255I2sStream::WriteData(uint64_t now, uint32_t value) {
    if (InReset()) return;
    Settle(now);
    if (PioOnTx(true)) {
        if (tx_.Level() >= kFifoDepth) return;
        tx_.Put();
        values_->TxPut(value);
        if (LinkReadsTx()) host_->Queue(&value, sizeof(value), bit_clock_->Rate());
    } else if (rx_.Level() != 0u) {
        const std::vector<uint32_t> word{value};
        PushWords(1u, &word);
    }
    if ((sacr0_ & kSacr0Efwr) != 0u) dma_->OnPortChange();
    irq_->Refresh();
}

/* Intel PXA255 Developer's Manual section 14.6.1 (page 14-8): with ENB clear "any read accesses to the
   Data Register (SADR), by the processor, or by the DMA controller is returned with zeros"; section
   14.3.3 (page 14-5) DREC: "Any read operations by the DMA/CPU are returned with zeros". */
uint32_t Pxa255I2sStream::ReadData(uint64_t now) {
    if (!enabled_ || InReset() || (sacr1_ & kSacr1Drec) != 0u) return 0u;
    Settle(now);
    uint32_t value = 0u;
    if (PioOnTx(false)) {
        if (tx_.Level() != 0u) {
            const uint64_t         before = tx_.Moved();
            DmaBurstFifo::Arrivals arrivals;
            std::vector<uint32_t>  word;
            tx_.Take(1u, &arrivals);
            values_->TxTake(tx_, arrivals, before, 1u, &word);
            value = word[0];
        }
    } else if (rx_.Level() < kFifoDepth) {
        rx_.Put();
        value = values_->RxPop();
    }
    if ((sacr0_ & kSacr0Efwr) != 0u) dma_->OnPortChange();
    irq_->Refresh();
    return value;
}

void Pxa255I2sStream::ResetLine() {
    Settle(clock_->Cycles());
    enabled_     = false;
    gated_sacr1_ = 0u;
    out_frames_  = 0u;
    in_frames_   = 0u;
    resets_->Cancel();
    sacr0_ = kSacr0Reset;
    sacr1_ = 0u;
    ConfigureThresholds();
    tx_.Clear();
    values_->TxClear();
    values_->LoopClear();
    values_->ClearLastSent();
    ClearRx();
    bit_clock_->ResetLine();
    host_->Stop();
}

void Pxa255I2sStream::Save(StateWriter& w) {
    Settle(clock_->Cycles());
    w.Write("sacr0", sacr0_);
    w.Write("sacr1", sacr1_);
    w.Write<uint8_t>("i2s_enabled", enabled_ ? 1u : 0u);
    w.Write<uint32_t>("i2s_gated_sacr1", gated_sacr1_);
    w.Write<uint64_t>("i2s_out_frames", out_frames_);
    w.Write<uint64_t>("i2s_in_frames", in_frames_);
    w.Write<uint32_t>("i2s_tx_level", tx_.Level());
    w.Write<uint64_t>("i2s_tx_moved", tx_.Moved());
    w.Write<uint32_t>("i2s_rx_free", rx_.Level());
    w.Write<uint64_t>("i2s_rx_moved", rx_.Moved());
    out_law_.Save(w, "i2s_out");
    in_law_.Save(w, "i2s_in");
}

void Pxa255I2sStream::Restore(StateReader& r) {
    uint8_t  enabled  = 0;
    uint32_t tx_level = 0, rx_free = 0;
    uint64_t tx_moved = 0, rx_moved = 0;
    r.Read("sacr0", sacr0_);
    r.Read("sacr1", sacr1_);
    r.Read("i2s_enabled", enabled);
    r.Read("i2s_gated_sacr1", gated_sacr1_);
    r.Read("i2s_out_frames", out_frames_);
    r.Read("i2s_in_frames", in_frames_);
    r.Read("i2s_tx_level", tx_level);
    r.Read("i2s_tx_moved", tx_moved);
    r.Read("i2s_rx_free", rx_free);
    r.Read("i2s_rx_moved", rx_moved);
    out_law_.Restore(r, "i2s_out");
    in_law_.Restore(r, "i2s_in");
    ConfigureThresholds();
    tx_.Restore(tx_level, tx_moved);
    rx_.Restore(rx_free, rx_moved);
    tx_.SetSupply(0u);
    rx_.SetSupply(0u);
    enabled_ = enabled != 0u;
    host_->Stop();
}

REGISTER_SERVICE(Pxa255I2sStream);
