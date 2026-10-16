#include "omap3530_mcbsp_port_clock.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/state/state_stream.h>
#include "omap3530_id.h"
#include "omap3530_mcbsp_clock_inputs.h"
#include "omap3530_mcbsp_wiring.h"

#include <utility>

namespace cerfcore {

namespace mb = omap3530_mcbsp;

template <uint32_t kPort>
bool Omap3530McBspPortClock<kPort>::ShouldRegister() {
    return emu_.Get<BoardContext>().GetSocId() == SocId::Omap3530;
}

template <uint32_t kPort>
void Omap3530McBspPortClock<kPort>::OnReady() {
    clock_  = &emu_.Get<GuestCycleClock>();
    inputs_ = &emu_.Get<Omap3530McBspClockInputs>();
    peer_   = emu_.Get<Omap3530McBspWiring>().Peer(kPort);
    Rebuild(Dir::Tx);
    Rebuild(Dir::Rx);
}

template <uint32_t kPort>
bool Omap3530McBspPortClock<kPort>::Rebuild(Dir dir) {
    Omap3530McBspFrame next;
    const char*        error   = next.Build(regs_.frame, dir);
    const bool         changed = error != layout_error_[Idx(dir)] || !(next == layout_[Idx(dir)]);
    layout_[Idx(dir)]       = std::move(next);
    layout_error_[Idx(dir)] = error;
    return changed;
}

template <uint32_t kPort>
void Omap3530McBspPortClock<kPort>::SetListener(Dir dir, Omap3530McBspTimingListener* listener) {
    listener_[Idx(dir)] = listener;
    if (listener_[Idx(Dir::Tx)] == nullptr || listener_[Idx(Dir::Rx)] == nullptr) return;
    if (peer_ != nullptr) peer_->SetPeerListener(this);
    inputs_->RegisterListener([this] { OnInputs(); });
    clock_->RegisterRateListener([this] { OnCpuRate(); });
}

template <uint32_t kPort>
bool Omap3530McBspPortClock<kPort>::PeerDrives() const {
    return peer_ != nullptr && peer_->DrivesClockAndSync();
}

template <uint32_t kPort>
GuestCycleClock::Rate Omap3530McBspPortClock<kPort>::ClkxPinRate() const {
    if ((regs_.pcr & mb::kPcrClkxm) != 0u || !PeerDrives()) return GuestCycleClock::Rate{0u, 1u};
    return peer_->BitRate();
}

/* OMAP35x TRM SPRUF98Y Table 21-76 SCLKME (page 3081) with Table 21-60 CLKSM (page 3071): CLKS pin,
   McBSPi_ICLK, CLKR pin or CLKX pin; CLKG frequency = input clock frequency / (CLKGDV + 1). */
template <uint32_t kPort>
typename Omap3530McBspPortClock<kPort>::SrgSource Omap3530McBspPortClock<kPort>::SrgSourceSelected() const {
    const bool sclkme = (regs_.pcr & mb::kPcrSclkme) != 0u;
    const bool clksm  = (regs_.srgr2 & mb::kSrgr2Clksm) != 0u;
    if (!sclkme) return clksm ? SrgSource::Iclk : SrgSource::Clks;
    if (!clksm && !inputs_->ClkrFromClkx(kPort)) return SrgSource::None;
    return SrgSource::ClkxPin;
}

template <uint32_t kPort>
GuestCycleClock::Rate Omap3530McBspPortClock<kPort>::SrgInput() const {
    switch (SrgSourceSelected()) {
    case SrgSource::Iclk:    return inputs_->Iclk(kPort);
    case SrgSource::Clks:    return inputs_->Clks(kPort);
    case SrgSource::ClkxPin: return ClkxPinRate();
    case SrgSource::None:    break;
    }
    return GuestCycleClock::Rate{0u, 1u};
}

template <uint32_t kPort>
bool Omap3530McBspPortClock<kPort>::SrgFromIclk() const {
    return SrgSourceSelected() == SrgSource::Iclk;
}

template <uint32_t kPort>
bool Omap3530McBspPortClock<kPort>::DependsOnPeer(const Derived& d) const {
    if (d.clk == Clk::Peer || d.fs == Fs::Peer || d.fs == Fs::Resync) return true;
    return UsesClkg(d) && SrgSourceSelected() == SrgSource::ClkxPin && ClkxPinRate().num != 0u;
}

/* SPRUF98Y Table 21-48 (page 3062): GRST 1 drives CLKG, FRST 1 generates FSG "after (FPER+1) number of
   CLKG clocks"; Table 21-60 (page 3071) FPER + 1 = the frame period, 1 to 4096 CLKG periods. */
template <uint32_t kPort>
GuestCycleClock::Rate Omap3530McBspPortClock<kPort>::ClkgRate() const {
    const GuestCycleClock::Rate in = SrgInput();
    if ((regs_.spcr2 & mb::kSpcr2Grst) == 0u || in.num == 0u) return GuestCycleClock::Rate{0u, 1u};
    return in.Scaled(1u, mb::Clkgdv(regs_.srgr1) + 1u).Reduced();
}

template <uint32_t kPort>
void Omap3530McBspPortClock<kPort>::UpdateSrg(uint64_t now) {
    const GuestCycleClock::Rate rate = ClkgRate();
    const bool want = rate.num != 0u;
    const GuestCycleClock::Rate core = clock_->ClockRate();
    if (want && !srg_.clkg_on) {
        clkg_.SetRate(core, rate);
        clkg_.Start(now);
        srg_.clkg_on = true;
    } else if (!want) {
        srg_.clkg_on = false;
    } else if (!(rate == srg_.clkg_rate)) {
        clkg_.Rescale(now, core, rate);
    }
    srg_.clkg_rate = rate;

    const uint32_t period = mb::Fper(regs_.srgr2) + 1u;
    if ((regs_.spcr2 & mb::kSpcr2Frst) == 0u || !srg_.clkg_on) {
        srg_.fsg_on = false;
    } else if (!srg_.fsg_on) {
        srg_.fsg_on    = true;
        srg_.fsg_first = clkg_.TicksAt(now) + period;
    }
    srg_.period = period;
}

/* SPRUF98Y Table 21-60 (page 3071): GSYNC resynchronizes CLKG and generates FSG only after an FSR
   transition, FPER a don't care; FSGM 0 generates FSX "when transmit buffer is not empty", gated by
   the buffer empty condition, FSGM 1 drives FSX with FSG; Table 21-76 FSXP / FSRP 1 = active low. */
template <uint32_t kPort>
void Omap3530McBspPortClock<kPort>::UpdateModes() {
    const uint32_t pcr   = regs_.pcr;
    const bool     gsync = (regs_.srgr2 & mb::kSrgr2Gsync) != 0u;
    const bool     iclk  = SrgFromIclk();
    const bool     fsxm  = (pcr & mb::kPcrFsxm) != 0u;
    Derived&       tx    = d_[Idx(Dir::Tx)];
    tx           = Derived{};
    tx.fsx_shape = fsxm;
    if (fsxm) {
        tx.fsxp = (pcr & mb::kPcrFsxp) != 0u;
        tx.fwid = mb::Fwid(regs_.srgr1);
    }
    if ((pcr & mb::kPcrClkxm) != 0u || gsync) {
        tx.clk = srg_.clkg_on ? Clk::Clkg : Clk::None;
    } else {
        tx.clk = PeerDrives() ? Clk::Peer : Clk::None;
    }
    bool active_low = (pcr & mb::kPcrFsxp) != 0u;
    if (gsync && !iclk) {
        tx.fs      = inputs_->FsrFromFsx(kPort) && !fsxm && PeerDrives() ? Fs::Resync : Fs::None;
        active_low = (pcr & mb::kPcrFsrp) != 0u;
    } else if (gsync || (fsxm && (regs_.srgr2 & mb::kSrgr2Fsgm) != 0u)) {
        tx.fs = srg_.fsg_on ? Fs::Fsg : Fs::None;
    } else if (fsxm) {
        tx.fs = srg_.fsg_on ? Fs::Gated : Fs::None;
    } else {
        tx.fs = PeerDrives() ? Fs::Peer : Fs::None;
    }
    if (tx.fs == Fs::Peer || tx.fs == Fs::Resync) {
        tx.fb   = peer_->FrameBits();
        tx.edge = active_low ? peer_->SyncFall() : peer_->SyncRise();
    }
    UpdateReceiveModes();
}

/* SPRUF98Y Figure 21-26 (page 2993), Table 21-15 (page 2994): CLKR_int is CLKG with GSYNC or CLKRM, FSR_int
   FSG with GSYNC or FSRM and CLKRM, else the pins; Table 7-80 (page 881): McBSP2 CLKR is the McBSP2_CLKX
   pin, FSR the McBSP2_FSX pin; page 2973: McBSP2-5 have no CLKR / FSR pins. */
template <uint32_t kPort>
void Omap3530McBspPortClock<kPort>::UpdateReceiveModes() {
    const uint32_t pcr   = regs_.pcr;
    const bool     gsync = (regs_.srgr2 & mb::kSrgr2Gsync) != 0u;
    const bool     clkrm = (pcr & mb::kPcrClkrm) != 0u;
    const bool     fsrm  = (pcr & mb::kPcrFsrm) != 0u;
    const bool     fsxm  = (pcr & mb::kPcrFsxm) != 0u;
    const Derived& tx    = d_[Idx(Dir::Tx)];
    Derived&       rx    = d_[Idx(Dir::Rx)];
    rx               = Derived{};
    rx_source_error_ = nullptr;
    if ((regs_.spcr1 & mb::kSpcr1Alb) != 0u) {
        rx_source_error_ = "SPCR1 ALB selects the analog loopback of the transmitter into the receiver";
        return;
    }
    const Clk clkg = srg_.clkg_on ? Clk::Clkg : Clk::None;
    if (gsync || clkrm) {
        rx.clk = clkg;
    } else if (!inputs_->ClkrFromClkx(kPort)) {
        rx.clk = Clk::None;
    } else {
        rx.clk = (pcr & mb::kPcrClkxm) != 0u ? clkg : (PeerDrives() ? Clk::Peer : Clk::None);
    }
    const bool iclk      = SrgFromIclk();
    bool       from_peer = false;
    if (gsync) {
        rx.fs     = iclk ? (srg_.fsg_on ? Fs::Fsg : Fs::None) : tx.fs;
        from_peer = rx.fs == Fs::Resync;
    } else if (fsrm && clkrm) {
        rx.fs = srg_.fsg_on ? Fs::Fsg : Fs::None;
    } else if (!inputs_->FsrFromFsx(kPort)) {
        rx.fs = fsrm && srg_.fsg_on ? Fs::Fsg : Fs::None;
    } else if (!fsxm) {
        rx.fs     = PeerDrives() ? Fs::Peer : Fs::None;
        from_peer = rx.fs == Fs::Peer;
    } else if ((regs_.srgr2 & mb::kSrgr2Fsgm) != 0u) {
        rx.fs = srg_.fsg_on ? Fs::Fsg : Fs::None;
    } else if (srg_.fsg_on) {
        rx_source_error_ = "the receive frame sync is the transmitter's buffer-gated FSX output (FSXM 1, FSGM 0)";
    }
    rx.fsx_shape = !from_peer && fsxm && rx.fs == Fs::Fsg && tx.fs == Fs::Fsg;
    if (rx.fsx_shape) {
        rx.fsxp = tx.fsxp;
        rx.fwid = tx.fwid;
    }
    if (from_peer) {
        rx.fb   = peer_->FrameBits();
        rx.edge = (pcr & mb::kPcrFsrp) != 0u ? peer_->SyncFall() : peer_->SyncRise();
    }
}

template <uint32_t kPort>
bool Omap3530McBspPortClock<kPort>::UsesClkg(const Derived& d) {
    return d.clk == Clk::Clkg || d.fs == Fs::Fsg || d.fs == Fs::Gated || d.fs == Fs::Resync;
}

template <uint32_t kPort>
bool Omap3530McBspPortClock<kPort>::SameGrid(const Derived& a, const Srg& sa, const Derived& b, const Srg& sb) {
    if (a.clk != b.clk || a.fs != b.fs || a.fsx_shape != b.fsx_shape) return false;
    if (a.fsx_shape && (a.fsxp != b.fsxp || a.fwid != b.fwid)) return false;
    if ((a.fs == Fs::Peer || a.fs == Fs::Resync) && (a.edge != b.edge || a.fb != b.fb)) return false;
    if ((a.fs == Fs::Fsg || a.fs == Fs::Gated) && (sa.fsg_first != sb.fsg_first || sa.period != sb.period)) {
        return false;
    }
    return !UsesClkg(a) || sa.clkg_on == sb.clkg_on;
}

template <uint32_t kPort>
typename Omap3530McBspPortClock<kPort>::Change
Omap3530McBspPortClock<kPort>::Classify(Dir dir, const Derived& before, const Srg& srg_before,
                                        bool layout_changed) const {
    const Derived& now = d_[Idx(dir)];
    if (layout_changed || !SameGrid(before, srg_before, now, srg_)) return Change::Restarted;
    if (UsesClkg(now) && !(srg_before.clkg_rate == srg_.clkg_rate)) return Change::Rescaled;
    return Change::None;
}

template <uint32_t kPort>
typename Omap3530McBspPortClock<kPort>::Changes
Omap3530McBspPortClock<kPort>::Refresh(uint64_t now, bool tx_layout_changed, bool rx_layout_changed) {
    const Derived tx_before  = d_[Idx(Dir::Tx)];
    const Derived rx_before  = d_[Idx(Dir::Rx)];
    const Srg     srg_before = srg_;
    UpdateSrg(now);
    UpdateModes();
    Changes c;
    c.tx = Classify(Dir::Tx, tx_before, srg_before, tx_layout_changed);
    c.rx = Classify(Dir::Rx, rx_before, srg_before, rx_layout_changed);
    return c;
}

template <uint32_t kPort>
typename Omap3530McBspPortClock<kPort>::Changes
Omap3530McBspPortClock<kPort>::Apply(const Omap3530McBspRegs& regs, uint64_t now) {
    const bool frame_written = !(regs.frame == regs_.frame);
    regs_ = regs;
    const bool tx_changed = frame_written && Rebuild(Dir::Tx);
    const bool rx_changed = frame_written && Rebuild(Dir::Rx);
    return Refresh(now, tx_changed, rx_changed);
}

template <uint32_t kPort>
void Omap3530McBspPortClock<kPort>::Notify(const Changes& changes, uint64_t now) {
    const Change c[2] = {changes.tx, changes.rx};
    for (size_t i = 0; i < 2u; ++i) {
        if (c[i] == Change::Restarted) {
            listener_[i]->OnTimingRestarted(now);
        } else if (c[i] == Change::Rescaled) {
            listener_[i]->OnTimingRescaled(now);
        }
    }
}

template <uint32_t kPort>
void Omap3530McBspPortClock<kPort>::WillChange(uint64_t now) {
    listener_[Idx(Dir::Tx)]->OnTimingWillChange(now);
    listener_[Idx(Dir::Rx)]->OnTimingWillChange(now);
}

template <uint32_t kPort>
void Omap3530McBspPortClock<kPort>::OnInputs() {
    const uint64_t now = clock_->Cycles();
    WillChange(now);
    Notify(Refresh(now, false, false), now);
}

template <uint32_t kPort>
void Omap3530McBspPortClock<kPort>::OnCpuRate() {
    const uint64_t now = clock_->Cycles();
    WillChange(now);
    if (srg_.clkg_on) clkg_.Rescale(now, clock_->ClockRate(), srg_.clkg_rate);
    listener_[Idx(Dir::Tx)]->OnTimingRescaled(now);
    listener_[Idx(Dir::Rx)]->OnTimingRescaled(now);
}

template <uint32_t kPort>
void Omap3530McBspPortClock<kPort>::OnPeerTimingWillChange(uint64_t now) { WillChange(now); }

template <uint32_t kPort>
typename Omap3530McBspPortClock<kPort>::Changes Omap3530McBspPortClock<kPort>::PeerChanges(uint64_t now, Change change) {
    Changes c      = Refresh(now, false, false);
    Change* out[2] = {&c.tx, &c.rx};
    for (size_t i = 0; i < 2u; ++i) {
        if (*out[i] != Change::Restarted && DependsOnPeer(d_[i])) *out[i] = change;
    }
    return c;
}

template <uint32_t kPort>
void Omap3530McBspPortClock<kPort>::OnPeerRestarted(uint64_t now) {
    Notify(PeerChanges(now, Change::Restarted), now);
}

template <uint32_t kPort>
void Omap3530McBspPortClock<kPort>::OnPeerRescaled(uint64_t now) {
    Notify(PeerChanges(now, Change::Rescaled), now);
}

template <uint32_t kPort>
bool Omap3530McBspPortClock<kPort>::Running(Dir dir) const {
    const Derived& d = d_[Idx(dir)];
    return layout_error_[Idx(dir)] == nullptr && d.clk != Clk::None && d.fs != Fs::None;
}

template <uint32_t kPort>
void Omap3530McBspPortClock<kPort>::RequireRunnable(Dir dir, uint64_t now) const {
    const bool rx    = dir == Dir::Rx;
    auto&      fatal = emu_.Get<Fatal>();
    if (layout_error_[Idx(dir)] != nullptr) fatal.Die("McBSP%u: %s", kPort, layout_error_[Idx(dir)]);
    if (rx && rx_source_error_ != nullptr) fatal.Die("McBSP%u: %s; not modelled", kPort, rx_source_error_);
    const Omap3530McBspFrame& layout = layout_[Idx(dir)];
    if (!Running(dir) || layout.TakeCount() == 0u) return;
    const uint32_t period = FrameShape(dir, FirstFrameAfter(dir, now)).period;
    if (period < layout.DataBits()) {
        const char* side = rx ? "receive" : "transmit";
        fatal.Die("McBSP%u: the %s frame sync every %u bits comes before the %u data bits of a frame end; %s "
                  "frame-sync errors are not modelled", kPort, side, period, layout.DataBits(), side);
    }
}

template <uint32_t kPort>
uint64_t Omap3530McBspPortClock<kPort>::ClockCycleOfTick(const Derived& d, uint64_t tick) const {
    return d.clk == Clk::Peer ? peer_->CycleOfBit(tick) : clkg_.CycleOfTick(tick);
}

template <uint32_t kPort>
uint64_t Omap3530McBspPortClock<kPort>::ClockTickAtOrAfter(const Derived& d, uint64_t cycle) const {
    const uint64_t t = d.clk == Clk::Peer ? peer_->BitsAt(cycle) : clkg_.TicksAt(cycle);
    return ClockCycleOfTick(d, t) == cycle ? t : t + 1u;
}

template <uint32_t kPort>
uint64_t Omap3530McBspPortClock<kPort>::StartCycle(const Derived& d, uint64_t n) const {
    if (d.fs == Fs::Fsg || d.fs == Fs::Gated) return clkg_.CycleOfTick(srg_.fsg_first + (n - 1u) * srg_.period);
    return peer_->CycleOfBit(n * d.fb + d.edge);
}

template <uint32_t kPort>
uint64_t Omap3530McBspPortClock<kPort>::FrameTick0(const Derived& d, uint64_t n) const {
    if (d.clk == Clk::Peer && d.fs == Fs::Peer) return n * d.fb + d.edge;
    if (d.clk == Clk::Clkg && (d.fs == Fs::Fsg || d.fs == Fs::Gated)) {
        return srg_.fsg_first + (n - 1u) * srg_.period;
    }
    return ClockTickAtOrAfter(d, StartCycle(d, n));
}

template <uint32_t kPort>
bool Omap3530McBspPortClock<kPort>::AnchorResync(const Derived& d, uint64_t n, RatedTickCount& at) const {
    if (!at.SetRate(clock_->ClockRate(), srg_.clkg_rate)) return false;
    at.Start(StartCycle(d, n));
    return true;
}

template <uint32_t kPort>
uint64_t Omap3530McBspPortClock<kPort>::FrameBitCycle(Dir dir, uint64_t n, uint64_t bit) const {
    const Derived& d = d_[Idx(dir)];
    if (d.fs != Fs::Resync) return ClockCycleOfTick(d, FrameTick0(d, n) + bit);
    RatedTickCount at;
    AnchorResync(d, n, at);
    return at.CycleOfTick(bit);
}

template <uint32_t kPort>
uint64_t Omap3530McBspPortClock<kPort>::SlotEndCycle(Dir dir, uint64_t take) const {
    const Omap3530McBspFrame& layout = layout_[Idx(dir)];
    const uint32_t            takes  = layout.TakeCount();
    const auto&               slot   = layout.TakeSlot(static_cast<uint32_t>(take % takes));
    return FrameBitCycle(dir, take / takes, slot.start + slot.bits);
}

template <uint32_t kPort>
uint64_t Omap3530McBspPortClock<kPort>::FirstTakeEndingAfter(Dir dir, uint64_t lo, uint64_t hi, uint64_t now) const {
    while (lo < hi) {
        const uint64_t mid = lo + (hi - lo) / 2u;
        if (SlotEndCycle(dir, mid) > now) {
            hi = mid;
        } else {
            lo = mid + 1u;
        }
    }
    return lo;
}

template <uint32_t kPort>
uint64_t Omap3530McBspPortClock<kPort>::FirstFrameAfter(Dir dir, uint64_t cycle) const {
    const Derived& d = d_[Idx(dir)];
    if (d.fs == Fs::Fsg || d.fs == Fs::Gated) {
        const uint64_t t = clkg_.TicksAt(cycle);
        return t < srg_.fsg_first ? 1u : (t - srg_.fsg_first) / srg_.period + 2u;
    }
    const uint64_t b = peer_->BitsAt(cycle);
    return b < d.edge ? 0u : (b - d.edge) / d.fb + 1u;
}

template <uint32_t kPort>
Omap3530McBspDxFrame Omap3530McBspPortClock<kPort>::FrameShape(Dir dir, uint64_t n) const {
    const Derived& d = d_[Idx(dir)];
    Omap3530McBspDxFrame f;
    f.index = n;
    if (d.fs == Fs::Resync) {
        RatedTickCount at;
        AnchorResync(d, n, at);
        f.period = static_cast<uint32_t>(at.TicksAt(StartCycle(d, n + 1u)));
    } else {
        f.period = static_cast<uint32_t>(FrameTick0(d, n + 1u) - FrameTick0(d, n));
    }
    if (d.fsx_shape) {
        const uint32_t width = d.fwid + 1u;
        f.has_sync  = true;
        f.sync_rise = d.fsxp ? width : 0u;
        f.sync_fall = d.fsxp ? 0u : width;
    } else if (d.fb != 0u) {
        f.has_sync  = true;
        f.sync_rise = (peer_->SyncRise() + d.fb - d.edge) % d.fb;
        f.sync_fall = (peer_->SyncFall() + d.fb - d.edge) % d.fb;
    }
    return f;
}

template <uint32_t kPort>
void Omap3530McBspPortClock<kPort>::Save(StateWriter& w) {
    const TickPosition p = srg_.clkg_on ? clkg_.PositionAt(clock_->Cycles()) : TickPosition{};
    w.Write<uint8_t>("mcbsp_clkg_on", srg_.clkg_on ? 1u : 0u);
    w.Write<uint64_t>("mcbsp_clkg_ticks", p.ticks);
    w.Write<uint64_t>("mcbsp_clkg_phase", p.phase);
    w.Write<uint64_t>("mcbsp_clkg_phase_den", p.phase_den);
    w.Write<uint8_t>("mcbsp_fsg_on", srg_.fsg_on ? 1u : 0u);
    w.Write<uint64_t>("mcbsp_fsg_first", srg_.fsg_first);
}

template <uint32_t kPort>
void Omap3530McBspPortClock<kPort>::Restore(StateReader& r) {
    uint8_t clkg_on = 0, fsg_on = 0;
    r.Read("mcbsp_clkg_on", clkg_on);
    r.Read("mcbsp_clkg_ticks", restored_clkg_.ticks);
    r.Read("mcbsp_clkg_phase", restored_clkg_.phase);
    r.Read("mcbsp_clkg_phase_den", restored_clkg_.phase_den);
    r.Read("mcbsp_fsg_on", fsg_on);
    r.Read("mcbsp_fsg_first", srg_.fsg_first);
    srg_.clkg_on = clkg_on != 0u;
    srg_.fsg_on  = fsg_on != 0u;
}

template <uint32_t kPort>
void Omap3530McBspPortClock<kPort>::PostRestore(const Omap3530McBspRegs& regs) {
    regs_ = regs;
    Rebuild(Dir::Tx);
    Rebuild(Dir::Rx);
    srg_.clkg_rate = ClkgRate();
    srg_.period    = mb::Fper(regs_.srgr2) + 1u;
    if (srg_.clkg_on) {
        clkg_.SetRate(clock_->ClockRate(), srg_.clkg_rate);
        clkg_.PlaceAt(clock_->Cycles(), restored_clkg_);
    }
    UpdateModes();
}

template class Omap3530McBspPortClock<1>;
template class Omap3530McBspPortClock<2>;
template class Omap3530McBspPortClock<3>;
template class Omap3530McBspPortClock<4>;
template class Omap3530McBspPortClock<5>;

REGISTER_SERVICE(Omap3530McBsp1PortClock);
REGISTER_SERVICE(Omap3530McBsp2PortClock);
REGISTER_SERVICE(Omap3530McBsp3PortClock);
REGISTER_SERVICE(Omap3530McBsp4PortClock);
REGISTER_SERVICE(Omap3530McBsp5PortClock);

}
