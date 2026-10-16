#include "omap3530_mcbsp_receiver.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/state/state_stream.h>

namespace cerfcore {

namespace {

constexpr uint32_t SyncSource(uint32_t port) {
    return port == 1u ? Omap3530Sdma::kSyncMcbsp1Rx
         : port == 2u ? Omap3530Sdma::kSyncMcbsp2Rx
         : port == 3u ? Omap3530Sdma::kSyncMcbsp3Rx
         : port == 4u ? Omap3530Sdma::kSyncMcbsp4Rx
                      : Omap3530Sdma::kSyncMcbsp5Rx;
}

/* OMAP35x TRM SPRUF98Y Table 10-4 (page 1053): McBSP1_IRQ_RX M_IRQ_60, McBSP2_IRQ_RX 63,
   MCBSP3_IRQ_RX 90, MCBSP4_IRQ_RX 55, MCBSP5_IRQ_RX 82. */
constexpr int RxIrq(uint32_t port) {
    return port == 1u ? 60 : port == 2u ? 63 : port == 3u ? 90 : port == 4u ? 55 : 82;
}

/* SPRUF98Y Table 21-50 RJUST (page 3064): 0 right-justify and zero-fill, 1 right-justify and sign-extend,
   2 left-justify and zero-fill the LSBs in DRR. */
uint32_t Justify(uint32_t word, uint32_t bits, uint32_t rjust) {
    if (bits >= 32u) return word;
    if (rjust == 2u) return word << (32u - bits);
    if (rjust == 1u && ((word >> (bits - 1u)) & 1u) != 0u) return word | (~0u << bits);
    return word;
}

}

template <uint32_t kPort>
void Omap3530McBspReceiver<kPort>::OnReady() {
    InitDirection(Dir::Rx, SyncSource(kPort), RxIrq(kPort));
}

template <uint32_t kPort>
uint32_t Omap3530McBspReceiver<kPort>::Receive(uint64_t t) {
    const auto&    layout = clock_->Layout(Dir::Rx);
    const auto&    slot   = layout.TakeSlot(static_cast<uint32_t>(t % Takes()));
    uint32_t       word   = 0u;
    if (Omap3530McBspPeer* peer = clock_->Peer()) {
        word = peer->DoutWord(clock_->FrameShape(Dir::Rx, t / Takes()), slot.start, slot.bits);
    }
    if (layout.Reverse()) word = Omap3530McBspFrame::Reversed(word, slot.bits);
    return Justify(word, slot.bits, rjust_);
}

/* SPRUF98Y section 21.4.4.2 (page 3009): an RSR-to-RB copy occurs only if the RB is not full; overrun
   is DRR not read, RB full and RSR full; section 21.4.4.1 (page 3008): new data then overwrites the RSR. */
template <uint32_t kPort>
void Omap3530McBspReceiver<kPort>::Land(uint32_t value) {
    if (!rsr_full_ && rb_.size() < kBufferWords) {
        rb_.push_back(value);
        UpdateRrdy();
        return;
    }
    rsr_full_ = true;
    rsr_word_ = value;
}

/* SPRUF98Y page 3000, reception steps 5 and 6: RRDY is asserted "When the programmed receive threshold
   is reached (THRSH1_REG[10:0] RTHRESHOLD field)"; "When the RB is empty, ... RRDY bit is cleared". */
template <uint32_t kPort>
void Omap3530McBspReceiver<kPort>::UpdateRrdy() {
    if (rb_.empty()) {
        rrdy_ = false;
    } else if (rb_.size() >= rrdy_thr_) {
        rrdy_ = true;
    }
}

template <uint32_t kPort>
void Omap3530McBspReceiver<kPort>::Settle(uint64_t now) {
    if (!enabled_ || !placed_ || Takes() == 0u) return;
    while (SlotEnd(pos_) <= now) {
        if (rsr_full_) {
            const uint64_t first_open = clock_->FirstFrameAfter(Dir::Rx, now) * Takes();
            const uint64_t last       = clock_->FirstTakeEndingAfter(Dir::Rx, pos_, first_open, now) - 1u;
            rsr_word_ = Receive(last);
            pos_      = last + 1u;
            return;
        }
        Land(Receive(pos_));
        ++pos_;
    }
}

template <uint32_t kPort>
void Omap3530McBspReceiver<kPort>::Place(uint64_t after) {
    placed_ = clock_->Running(Dir::Rx) && Takes() != 0u;
    if (placed_) pos_ = clock_->FirstFrameAfter(Dir::Rx, after) * Takes();
}

template <uint32_t kPort>
void Omap3530McBspReceiver<kPort>::TakeRegs(const Omap3530McBspRegs& regs) {
    enabled_  = (regs.spcr1 & omap3530_mcbsp::kSpcr1Rrst) != 0u;
    rrdy_thr_ = regs.thrsh1;
    rintm_    = omap3530_mcbsp::IntMode(regs.spcr1);
    rjust_    = omap3530_mcbsp::Rjust(regs.spcr1);
    SetRequestShape((regs.rccr & omap3530_mcbsp::kRccrRdmaen) != 0u, regs.thrsh1 + 1u);
}

template <uint32_t kPort>
void Omap3530McBspReceiver<kPort>::Disable() {
    enabled_  = false;
    rb_.clear();
    rrdy_     = false;
    placed_   = false;
    rsr_full_ = false;
    DropRequest();
    UpdateIrqLine(cycles_->Cycles());
}

template <uint32_t kPort>
void Omap3530McBspReceiver<kPort>::Apply(const Omap3530McBspRegs& regs, Change change, uint64_t now) {
    const bool was = enabled_;
    TakeRegs(regs);
    if (!was && enabled_) {
        ClearRequest();
        Place(now);
    } else if (was && !enabled_) {
        Disable();
    } else if (enabled_ && change == Change::Restarted) {
        Place(now);
    }
    UpdateRrdy();
    EndRequestAtThreshold();
    Revalidate(now);
}

/* SPRUF98Y Table 21-116 RUNDFLSTAT (page 3098): data read while underflow condition is undefined;
   section 21.4.4.2 (page 3009): a DRR read clears RFULL and another frame sync restarts the receiver. */
template <uint32_t kPort>
uint32_t Omap3530McBspReceiver<kPort>::ReadData() {
    const uint64_t now = cycles_->Cycles();
    Settle(now);
    if (!enabled_ || rb_.empty()) return 0u;
    const uint32_t value = rb_.front();
    rb_.pop_front();
    if (rsr_full_) {
        rb_.push_back(rsr_word_);
        rsr_full_ = false;
        Place(now);
    }
    UpdateRrdy();
    CountRequestWord();
    RefreshLines(now);
    return value;
}

template <uint32_t kPort>
uint32_t Omap3530McBspReceiver<kPort>::Occupied() {
    Settle(cycles_->Cycles());
    return static_cast<uint32_t>(rb_.size());
}

template <uint32_t kPort>
bool Omap3530McBspReceiver<kPort>::Ready() {
    Settle(cycles_->Cycles());
    return rrdy_;
}

template <uint32_t kPort>
bool Omap3530McBspReceiver<kPort>::Overrun() {
    Settle(cycles_->Cycles());
    return rsr_full_;
}

/* SPRUF98Y Table 21-114 THRSH1 (page 3096) and section 21.4.5 (page 3013): the request is asserted
   while the RB occupied locations are THRSH1 + 1 or more, and de-asserted after THRSH1 + 1 words. */
template <uint32_t kPort>
bool Omap3530McBspReceiver<kPort>::RequestCondition() const {
    return enabled_ && rb_.size() >= RequestWords();
}

template <uint32_t kPort>
uint64_t Omap3530McBspReceiver<kPort>::OccupiedCycle(uint64_t now, uint32_t n) const {
    if (enabled_ && rb_.size() >= n) return now;
    if (!enabled_ || !placed_ || rsr_full_ || n > kBufferWords || Takes() == 0u) return kNever;
    return SlotEnd(pos_ + (n - rb_.size()) - 1u);
}

template <uint32_t kPort>
uint64_t Omap3530McBspReceiver<kPort>::ConditionCycle(uint64_t now) const {
    return OccupiedCycle(now, RequestWords());
}

/* SPRUF98Y Table 21-50 (page 3064): RINTM 0 drives the receive interrupt with RRDY, 3 with RSYNCERR. */
template <uint32_t kPort>
bool Omap3530McBspReceiver<kPort>::IrqLevel() const {
    return rintm_ == 0u && rrdy_;
}

template <uint32_t kPort>
uint64_t Omap3530McBspReceiver<kPort>::IrqCycle(uint64_t now) const {
    return rintm_ == 0u ? OccupiedCycle(now, rrdy_thr_ > 1u ? rrdy_thr_ : 1u) : kNever;
}

template <uint32_t kPort>
void Omap3530McBspReceiver<kPort>::OnTimingWillChange(uint64_t now) { Settle(now); }

template <uint32_t kPort>
void Omap3530McBspReceiver<kPort>::OnTimingRestarted(uint64_t now) {
    if (enabled_) Place(now);
    Revalidate(now);
}

template <uint32_t kPort>
void Omap3530McBspReceiver<kPort>::Save(StateWriter& w) {
    Settle(cycles_->Cycles());
    SaveWords(w, rb_);
    w.Write<uint8_t>("mcbsp_rx_rrdy", rrdy_ ? 1u : 0u);
    w.Write<uint8_t>("mcbsp_rx_placed", placed_ ? 1u : 0u);
    w.Write<uint64_t>("mcbsp_rx_position", pos_);
    w.Write<uint8_t>("mcbsp_rx_rsr_full", rsr_full_ ? 1u : 0u);
    w.Write<uint32_t>("mcbsp_rx_rsr_word", rsr_word_);
    SaveRequest(w);
}

template <uint32_t kPort>
void Omap3530McBspReceiver<kPort>::Restore(StateReader& r) {
    RestoreWords(r, rb_);
    uint8_t rrdy = 0, placed = 0, rsr_full = 0;
    r.Read("mcbsp_rx_rrdy", rrdy);
    r.Read("mcbsp_rx_placed", placed);
    r.Read("mcbsp_rx_position", pos_);
    r.Read("mcbsp_rx_rsr_full", rsr_full);
    r.Read("mcbsp_rx_rsr_word", rsr_word_);
    RestoreRequest(r);
    rrdy_     = rrdy != 0u;
    placed_   = placed != 0u;
    rsr_full_ = rsr_full != 0u;
}

template <uint32_t kPort>
void Omap3530McBspReceiver<kPort>::PostRestore(const Omap3530McBspRegs& regs) {
    TakeRegs(regs);
    RestoreLines();
}

template class Omap3530McBspReceiver<1>;
template class Omap3530McBspReceiver<2>;
template class Omap3530McBspReceiver<3>;
template class Omap3530McBspReceiver<4>;
template class Omap3530McBspReceiver<5>;

REGISTER_SERVICE(Omap3530McBsp1Receiver);
REGISTER_SERVICE(Omap3530McBsp2Receiver);
REGISTER_SERVICE(Omap3530McBsp3Receiver);
REGISTER_SERVICE(Omap3530McBsp4Receiver);
REGISTER_SERVICE(Omap3530McBsp5Receiver);

}
