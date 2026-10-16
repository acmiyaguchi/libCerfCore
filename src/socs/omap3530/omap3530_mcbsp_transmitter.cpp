#include "omap3530_mcbsp_transmitter.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/state/state_stream.h>

#include <cstddef>

namespace {

constexpr uint32_t SyncSource(uint32_t port) {
    return port == 1u ? Omap3530Sdma::kSyncMcbsp1Tx
         : port == 2u ? Omap3530Sdma::kSyncMcbsp2Tx
         : port == 3u ? Omap3530Sdma::kSyncMcbsp3Tx
         : port == 4u ? Omap3530Sdma::kSyncMcbsp4Tx
                      : Omap3530Sdma::kSyncMcbsp5Tx;
}

/* OMAP35x TRM SPRUF98Y Table 10-4 (page 1053): McBSP1_IRQ_TX M_IRQ_59, McBSP2_IRQ_TX 62,
   MCBSP3_IRQ_TX 89, MCBSP4_IRQ_TX 54, MCBSP5_IRQ_TX 81. */
constexpr int TxIrq(uint32_t port) {
    return port == 1u ? 59 : port == 2u ? 62 : port == 3u ? 89 : port == 4u ? 54 : 81;
}

uint32_t Encode(uint32_t value, uint32_t bits, bool reverse) {
    const uint32_t word = bits >= 32u ? value : value & ((1u << bits) - 1u);
    return reverse ? Omap3530McBspFrame::Reversed(word, bits) : word;
}

}

template <uint32_t kPort>
void Omap3530McBspTransmitter<kPort>::OnReady() {
    InitDirection(Dir::Tx, SyncSource(kPort), TxIrq(kPort));
}

template <uint32_t kPort>
uint32_t Omap3530McBspTransmitter<kPort>::XbCount() const {
    const uint32_t n = static_cast<uint32_t>(words_.size());
    return fresh_ ? n - 1u : n;
}

template <uint32_t kPort>
void Omap3530McBspTransmitter<kPort>::Settle(uint64_t now) {
    if (!fresh_ || !placed_ || SlotEnd(pos_x_) > now) return;
    const uint64_t count = words_.size();
    if (SlotEnd(pos_x_ + count - 1u) <= now) {
        shifted_ = words_.back();
        words_.clear();
        fresh_    = false;
        reported_ = 0u;
        return;
    }
    const uint64_t lo = clock_->FirstTakeEndingAfter(Dir::Tx, pos_x_, pos_x_ + count - 1u, now) - pos_x_;
    shifted_ = words_[static_cast<size_t>(lo - 1u)];
    words_.erase(words_.begin(), words_.begin() + static_cast<std::ptrdiff_t>(lo));
    pos_x_   += lo;
    reported_ = reported_ > lo ? reported_ - static_cast<size_t>(lo) : 0u;
}

/* SPRUF98Y section 21.4.4.5 (page 3011): with the XB empty "the transmitter waits for the next frame-synchronization
   pulse before sending out the next frame"; page 3009: until DXR is loaded, each new frame sync sends the XSR data again. */
template <uint32_t kPort>
void Omap3530McBspTransmitter<kPort>::PlaceFresh(uint64_t now) {
    placed_ = clock_->Running(Dir::Tx) && Takes() != 0u;
    if (placed_) pos_x_ = clock_->FirstFrameAfter(Dir::Tx, now) * Takes();
}

template <uint32_t kPort>
bool Omap3530McBspTransmitter<kPort>::Reporting() const {
    return enabled_ && clock_->Peer() != nullptr && clock_->Running(Dir::Tx) && Takes() != 0u &&
           !clock_->FrameSyncGated(Dir::Tx);
}

/* SPRUF98Y page 3009: with XB empty "the previous data in the XSR is sent again" at each new frame sync; page 3011:
   zeros before the first DXR load after the release, and with FSXM 1 / FSGM 0 no frame sync while XB is empty. */
template <uint32_t kPort>
void Omap3530McBspTransmitter<kPort>::FillUntil(uint64_t frame) {
    if (!Reporting()) {
        if (next_report_ < frame) next_report_ = frame;
        return;
    }
    for (; next_report_ < frame; ++next_report_) {
        for (uint32_t s = 0; s < Takes(); ++s) Report(next_report_ * Takes() + s, dx_value_);
    }
}

template <uint32_t kPort>
void Omap3530McBspTransmitter<kPort>::FillComplete(uint64_t now) {
    if (!Reporting()) return;
    const uint64_t first = clock_->FirstFrameAfter(Dir::Tx, now);
    if (first != 0u) FillUntil(first - 1u);
}

template <uint32_t kPort>
void Omap3530McBspTransmitter<kPort>::ReportNew() {
    if (!placed_) return;
    for (size_t i = reported_; i < words_.size(); ++i) {
        const uint64_t t     = pos_x_ + i;
        const uint64_t frame = t / Takes();
        FillUntil(frame);
        Report(t, words_[i]);
        if (next_report_ <= frame) next_report_ = frame + 1u;
    }
    reported_ = words_.size();
}

template <uint32_t kPort>
void Omap3530McBspTransmitter<kPort>::Report(uint64_t t, uint32_t value) {
    const uint64_t n = t / Takes();
    const uint32_t s = static_cast<uint32_t>(t % Takes());
    if (clock_->Peer() != nullptr) {
        if (pend_valid_ && pend_frame_.index != n) FlushPending();
        if (!pend_valid_) {
            pend_valid_   = true;
            pend_before_  = dx_value_;
            pend_frame_   = clock_->FrameShape(Dir::Tx, n);
            pend_slots_   = clock_->Layout(Dir::Tx).Slots();
            pend_reverse_ = clock_->Layout(Dir::Tx).Reverse();
            pend_vals_.assign(Takes(), 0u);
            pend_set_.assign(Takes(), 0u);
        }
        pend_vals_[s] = value;
        pend_set_[s]  = 1u;
    }
    dx_value_ = value;
    if (s + 1u == Takes()) FlushPending();
}

template <uint32_t kPort>
void Omap3530McBspTransmitter<kPort>::FlushPending() {
    if (!pend_valid_) return;
    pend_valid_      = false;
    uint32_t current = pend_before_;
    uint32_t k       = 0u;
    for (const auto& slot : pend_slots_) {
        if (slot.kind == Omap3530McBspFrame::Kind::Disabled) continue;
        if (pend_set_[k] != 0u) current = pend_vals_[k];
        ++k;
        if (slot.kind == Omap3530McBspFrame::Kind::Masked) continue;
        pend_frame_.words.push_back({slot.start, slot.bits, Encode(current, slot.bits, pend_reverse_)});
    }
    clock_->Peer()->ReceiveFrame(pend_frame_);
}

template <uint32_t kPort>
void Omap3530McBspTransmitter<kPort>::TakeRegs(const Omap3530McBspRegs& regs) {
    enabled_  = (regs.spcr2 & omap3530_mcbsp::kSpcr2Xrst) != 0u;
    xrdy_thr_ = regs.thrsh2;
    xintm_    = omap3530_mcbsp::IntMode(regs.spcr2);
    SetRequestShape((regs.xccr & omap3530_mcbsp::kXccrXdmaen) != 0u, regs.thrsh2 + 1u);
}

template <uint32_t kPort>
void Omap3530McBspTransmitter<kPort>::Enable(uint64_t now) {
    reported_    = 0u;
    dx_value_    = 0u;
    shifted_     = 0u;
    next_report_ = clock_->Running(Dir::Tx) ? clock_->FirstFrameAfter(Dir::Tx, now) : 0u;
    ClearRequest();
}

template <uint32_t kPort>
void Omap3530McBspTransmitter<kPort>::Disable() {
    FillComplete(cycles_->Cycles());
    FlushPending();
    if (clock_->Peer() != nullptr) clock_->Peer()->FlushFrames();
    enabled_ = false;
    words_.clear();
    fresh_  = false;
    placed_ = false;
    DropRequest();
    UpdateIrqLine(cycles_->Cycles());
}

template <uint32_t kPort>
void Omap3530McBspTransmitter<kPort>::Replace(uint64_t now) {
    FlushPending();
    next_report_ = clock_->Running(Dir::Tx) ? clock_->FirstFrameAfter(Dir::Tx, now) : 0u;
    reported_    = 0u;
    if (fresh_) PlaceFresh(now);
    ReportNew();
}

template <uint32_t kPort>
void Omap3530McBspTransmitter<kPort>::Apply(const Omap3530McBspRegs& regs, Change change, uint64_t now) {
    const bool was = enabled_;
    TakeRegs(regs);
    if (!was && enabled_) {
        Enable(now);
    } else if (was && !enabled_) {
        Disable();
    } else if (enabled_ && change == Change::Restarted) {
        Replace(now);
    }
    EndRequestAtThreshold();
    Revalidate(now);
}

/* SPRUF98Y page 3011: at XRST release the transmitter "is in a transmitter ready state (... XRDY bit =1) and
   transmitter empty (... XEMPTY=0) state"; page 3028 NOTE: DXR is loaded "only when the transmitter is enabled". */
template <uint32_t kPort>
void Omap3530McBspTransmitter<kPort>::WriteData(uint32_t value) {
    if (!enabled_) return;
    const uint64_t now = cycles_->Cycles();
    Settle(now);
    if (!fresh_) {
        words_.assign(1u, value);
        fresh_    = true;
        reported_ = 0u;
        PlaceFresh(now);
        ReportNew();
    } else if (XbCount() < kBufferWords) {
        words_.push_back(value);
        ReportNew();
    }
    CountRequestWord();
    RefreshLines(now);
}

template <uint32_t kPort>
uint32_t Omap3530McBspTransmitter<kPort>::FreeLocations() {
    Settle(cycles_->Cycles());
    return kBufferWords - XbCount();
}

template <uint32_t kPort>
bool Omap3530McBspTransmitter<kPort>::ShiftRegisterLoaded() {
    Settle(cycles_->Cycles());
    return fresh_;
}

/* SPRUF98Y section 21.4.2.6 step 2 (page 3001): XRDY "is set as long as the buffer contains at least
   the transmit threshold number of free locations (THRSH2 XTHRESHOLD field)". */
template <uint32_t kPort>
bool Omap3530McBspTransmitter<kPort>::Ready() {
    Settle(cycles_->Cycles());
    return FreeAtLeast(xrdy_thr_);
}

template <uint32_t kPort>
bool Omap3530McBspTransmitter<kPort>::FreeAtLeast(uint32_t n) const {
    return enabled_ && kBufferWords - XbCount() >= n;
}

template <uint32_t kPort>
uint64_t Omap3530McBspTransmitter<kPort>::FreeCycle(uint64_t now, uint32_t n) const {
    if (FreeAtLeast(n)) return now;
    if (!enabled_ || n > kBufferWords || !fresh_ || !placed_) return kNever;
    const uint64_t takes = XbCount() - (kBufferWords - n);
    return SlotEnd(pos_x_ + takes - 1u);
}

/* SPRUF98Y section 21.4.5 (page 3013): the transmit DMA request is asserted while the free locations are
   THRSH2 + 1 or more and de-asserted after THRSH2 + 1 words. */
template <uint32_t kPort>
bool Omap3530McBspTransmitter<kPort>::RequestCondition() const {
    return FreeAtLeast(RequestWords());
}

template <uint32_t kPort>
uint64_t Omap3530McBspTransmitter<kPort>::ConditionCycle(uint64_t now) const {
    return FreeCycle(now, RequestWords());
}

/* SPRUF98Y Table 21-48 (page 3062): XINTM 0 drives the transmit interrupt with XRDY, 3 with XSYNCERR. */
template <uint32_t kPort>
bool Omap3530McBspTransmitter<kPort>::IrqLevel() const {
    return xintm_ == 0u && FreeAtLeast(xrdy_thr_);
}

template <uint32_t kPort>
uint64_t Omap3530McBspTransmitter<kPort>::IrqCycle(uint64_t now) const {
    return xintm_ == 0u ? FreeCycle(now, xrdy_thr_) : kNever;
}

template <uint32_t kPort>
void Omap3530McBspTransmitter<kPort>::OnTimingWillChange(uint64_t now) {
    Settle(now);
    FillComplete(now);
}

template <uint32_t kPort>
void Omap3530McBspTransmitter<kPort>::OnTimingRestarted(uint64_t now) {
    if (enabled_) Replace(now);
    Revalidate(now);
}

template <uint32_t kPort>
void Omap3530McBspTransmitter<kPort>::Save(StateWriter& w) {
    Settle(cycles_->Cycles());
    SaveWords(w, words_);
    w.Write<uint8_t>("mcbsp_tx_fresh", fresh_ ? 1u : 0u);
    w.Write<uint8_t>("mcbsp_tx_placed", placed_ ? 1u : 0u);
    w.Write<uint64_t>("mcbsp_tx_position", pos_x_);
    w.Write<uint32_t>("mcbsp_tx_shifted", shifted_);
    w.Write<uint64_t>("mcbsp_tx_next_report", next_report_);
    SaveRequest(w);
}

template <uint32_t kPort>
void Omap3530McBspTransmitter<kPort>::Restore(StateReader& r) {
    RestoreWords(r, words_);
    uint8_t fresh = 0, placed = 0;
    r.Read("mcbsp_tx_fresh", fresh);
    r.Read("mcbsp_tx_placed", placed);
    r.Read("mcbsp_tx_position", pos_x_);
    r.Read("mcbsp_tx_shifted", shifted_);
    r.Read("mcbsp_tx_next_report", next_report_);
    RestoreRequest(r);
    fresh_  = fresh != 0u;
    placed_ = placed != 0u;
}

template <uint32_t kPort>
void Omap3530McBspTransmitter<kPort>::PostRestore(const Omap3530McBspRegs& regs) {
    TakeRegs(regs);
    pend_valid_ = false;
    reported_   = 0u;
    dx_value_   = shifted_;
    if (enabled_ && fresh_) ReportNew();
    RestoreLines();
}

template class Omap3530McBspTransmitter<1>;
template class Omap3530McBspTransmitter<2>;
template class Omap3530McBspTransmitter<3>;
template class Omap3530McBspTransmitter<4>;
template class Omap3530McBspTransmitter<5>;

REGISTER_SERVICE(Omap3530McBsp1Transmitter);
REGISTER_SERVICE(Omap3530McBsp2Transmitter);
REGISTER_SERVICE(Omap3530McBsp3Transmitter);
REGISTER_SERVICE(Omap3530McBsp4Transmitter);
REGISTER_SERVICE(Omap3530McBsp5Transmitter);
