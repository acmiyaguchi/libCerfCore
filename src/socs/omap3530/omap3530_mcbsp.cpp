#include "omap3530_prcm_stub_block.h"
#include "omap3530_mcbsp_port_clock.h"
#include "omap3530_mcbsp_receiver.h"
#include "omap3530_mcbsp_transmitter.h"

#include <cerfcore/core/fatal.h>
#include "../../execution/guest_cycle_clock.h"
#include <cerfcore/socs/guest_cpu_reset.h>

#include <algorithm>

namespace {

namespace mb = omap3530_mcbsp;

/* OMAP35x TRM SPRUF98Y Table 21-36 base addresses; Tables 21-37 / 21-38 register offsets; 0x04 and 0x0C
   are not mapped. */
constexpr uint32_t kOffDrr       = 0x00u;
constexpr uint32_t kOffDxr       = 0x08u;
constexpr uint32_t kOffSpcr2     = 0x10u;
constexpr uint32_t kOffSpcr1     = 0x14u;
constexpr uint32_t kOffRcr2      = 0x18u;
constexpr uint32_t kOffRcr1      = 0x1Cu;
constexpr uint32_t kOffXcr2      = 0x20u;
constexpr uint32_t kOffXcr1      = 0x24u;
constexpr uint32_t kOffSrgr2     = 0x28u;
constexpr uint32_t kOffSrgr1     = 0x2Cu;
constexpr uint32_t kOffMcr2      = 0x30u;
constexpr uint32_t kOffMcr1      = 0x34u;
constexpr uint32_t kOffRcera     = 0x38u;
constexpr uint32_t kOffPcr       = 0x48u;
constexpr uint32_t kOffXcerh     = 0x78u;
constexpr uint32_t kOffRev       = 0x7Cu;
constexpr uint32_t kOffRintclr   = 0x80u;
constexpr uint32_t kOffXintclr   = 0x84u;
constexpr uint32_t kOffRovflclr  = 0x88u;
constexpr uint32_t kOffSysconfig = 0x8Cu;
constexpr uint32_t kOffThrsh2    = 0x90u;
constexpr uint32_t kOffThrsh1    = 0x94u;
constexpr uint32_t kOffIrqstatus = 0xA0u;
constexpr uint32_t kOffIrqenable = 0xA4u;
constexpr uint32_t kOffWakeupen  = 0xA8u;
constexpr uint32_t kOffXccr      = 0xACu;
constexpr uint32_t kOffRccr      = 0xB0u;
constexpr uint32_t kOffXbuffstat = 0xB4u;
constexpr uint32_t kOffRbuffstat = 0xB8u;
constexpr uint32_t kOffSselcr    = 0xBCu;
constexpr uint32_t kOffStatus    = 0xC0u;

constexpr uint32_t kXcerOff[8] = {0x40u, 0x44u, 0x54u, 0x58u, 0x64u, 0x68u, 0x74u, 0x78u};
constexpr uint32_t kRcerOff[8] = {0x38u, 0x3Cu, 0x4Cu, 0x50u, 0x5Cu, 0x60u, 0x6Cu, 0x70u};

/* SPRUF98Y Table 21-48 SPCR2 XEMPTY 2, XRDY 1; Table 21-50 SPCR1 RFULL 2, RRDY 1, RSYNCERR 3; Table 21-110
   SYSCONFIG SOFTRESET 1; Table 21-130 SSELCR SIDETONEEN 10; Table 21-122 XCCR EXTCLKGATE 15,
   XFULL_CYCLE 11, DLB 5, XDISABLE 0. */
constexpr uint32_t kSpcrRdy            = 1u << 1;
constexpr uint32_t kSpcrEmptyFull      = 1u << 2;
constexpr uint32_t kSpcrSyncErr        = 1u << 3;
constexpr uint32_t kSysconfigSoftReset = 1u << 1;
constexpr uint32_t kSselcrSidetoneen   = 1u << 10;
constexpr uint32_t kXccrUnmodelled     = (1u << 15) | (1u << 11) | (1u << 5) | (1u << 0);

/* SPRUF98Y page 3028 NOTE: SPCR1, SPCR2, PCR, RCR1, RCR2, XCR1, XCR2, THRSH2, XCCR, SYSCONFIG, SRGR1 and SRGR2
   "should only be modified when the affected portion of the serial port is in its reset state"; "Modification
   on-the-fly has no effect if a reset is not done first". */
constexpr uint32_t kTxSpcr2   = 0x0030u;
constexpr uint32_t kRxSpcr1   = 0xE030u;
constexpr uint32_t kTxPcr     = 0x2A0Au;
constexpr uint32_t kRxPcr     = 0x1505u;
constexpr uint32_t kClkgPcr   = 0x0080u;
constexpr uint32_t kClkgSrgr1 = 0x00FFu;
constexpr uint32_t kFsgSrgr1  = 0xFF00u;
constexpr uint32_t kTxSrgr2   = 0x1000u;
constexpr uint32_t kClkgSrgr2 = 0xE000u;
constexpr uint32_t kFsgSrgr2  = 0x0FFFu;
constexpr uint32_t kTxXccr    = 0xF821u;

struct Portions {
    bool tx, rx, clkg, fsg;
};

Portions InReset(uint32_t spcr1, uint32_t spcr2) {
    const bool clkg = (spcr2 & mb::kSpcr2Grst) == 0u;
    return {(spcr2 & mb::kSpcr2Xrst) == 0u, (spcr1 & mb::kSpcr1Rrst) == 0u, clkg,
            clkg || (spcr2 & mb::kSpcr2Frst) == 0u};
}

uint32_t Keep(uint32_t effective, uint32_t stored, uint32_t frozen) {
    return (effective & frozen) | (stored & ~frozen);
}

Omap3530McBspRegs Latch(const Omap3530McBspRegs& eff, const Omap3530McBspRegs& s, const Portions& open) {
    const uint32_t tx   = open.tx ? 0u : ~0u;
    const uint32_t rx   = open.rx ? 0u : ~0u;
    const uint32_t clkg = open.clkg ? 0u : ~0u;
    const uint32_t fsg  = open.fsg ? 0u : ~0u;
    Omap3530McBspRegs r = s;
    r.spcr2      = Keep(eff.spcr2, s.spcr2, tx & kTxSpcr2);
    r.spcr1      = Keep(eff.spcr1, s.spcr1, rx & kRxSpcr1);
    r.pcr        = Keep(eff.pcr, s.pcr, (tx & kTxPcr) | (rx & kRxPcr) | (clkg & kClkgPcr));
    r.srgr1      = Keep(eff.srgr1, s.srgr1, (clkg & kClkgSrgr1) | (fsg & kFsgSrgr1));
    r.srgr2      = Keep(eff.srgr2, s.srgr2, (tx & kTxSrgr2) | (clkg & kClkgSrgr2) | (fsg & kFsgSrgr2));
    r.thrsh2     = Keep(eff.thrsh2, s.thrsh2, tx);
    r.xccr       = Keep(eff.xccr, s.xccr, tx & kTxXccr);
    r.frame.xcr1 = Keep(eff.frame.xcr1, s.frame.xcr1, tx);
    r.frame.xcr2 = Keep(eff.frame.xcr2, s.frame.xcr2, tx);
    r.frame.rcr1 = Keep(eff.frame.rcr1, s.frame.rcr1, rx);
    r.frame.rcr2 = Keep(eff.frame.rcr2, s.frame.rcr2, rx);
    return r;
}

template <typename F>
constexpr void VisitRegs(Omap3530McBspRegs& r, F& field) {
    field("mcbsp_eff_spcr2", r.spcr2);
    field("mcbsp_eff_spcr1", r.spcr1);
    field("mcbsp_eff_pcr", r.pcr);
    field("mcbsp_eff_srgr1", r.srgr1);
    field("mcbsp_eff_srgr2", r.srgr2);
    field("mcbsp_eff_thrsh2", r.thrsh2);
    field("mcbsp_eff_thrsh1", r.thrsh1);
    field("mcbsp_eff_xccr", r.xccr);
    field("mcbsp_eff_rccr", r.rccr);
    field("mcbsp_eff_xcr1", r.frame.xcr1);
    field("mcbsp_eff_xcr2", r.frame.xcr2);
    field("mcbsp_eff_rcr1", r.frame.rcr1);
    field("mcbsp_eff_rcr2", r.frame.rcr2);
    field("mcbsp_eff_mcr1", r.frame.mcr1);
    field("mcbsp_eff_mcr2", r.frame.mcr2);
    field("mcbsp_eff_xcer", r.frame.xcer);
    field("mcbsp_eff_rcer", r.frame.rcer);
}

static_assert(StateVisitCoversAllBytes<Omap3530McBspRegs>(
                  [](Omap3530McBspRegs& r, StateFieldBytes& f) { VisitRegs(r, f); }),
              "VisitRegs must name every field of Omap3530McBspRegs");

constexpr uint32_t BaseOf(uint32_t port) {
    return port == 1u ? 0x48074000u
         : port == 2u ? 0x49022000u
         : port == 3u ? 0x49024000u
         : port == 4u ? 0x49026000u
                      : 0x48096000u;
}

uint32_t StoredMask(uint32_t off, uint32_t thrsh_mask) {
    switch (off) {
    case kOffSpcr2:     return 0x3F9u;
    case kOffSpcr1:     return 0xE0B9u;
    case kOffRcr2:
    case kOffXcr2:      return 0xFFFBu;
    case kOffRcr1:
    case kOffXcr1:      return 0x7FE0u;
    case kOffSrgr2:
    case kOffSrgr1:     return 0xFFFFu;
    case kOffMcr2:      return 0x3E3u;
    case kOffMcr1:      return 0x3E1u;
    case kOffPcr:       return 0x7FAFu;
    case kOffSysconfig: return 0x31Cu;
    case kOffThrsh2:
    case kOffThrsh1:    return thrsh_mask;
    case kOffIrqenable: return 0x5FBFu;
    case kOffWakeupen:  return 0x478Fu;
    case kOffXccr:      return 0xF829u;
    case kOffRccr:      return 0x809u;
    case kOffSselcr:    return 0x7FFu;
    }
    return off >= kOffRcera && off <= kOffXcerh && (off & 3u) == 0u ? 0xFFFFu : 0u;
}

template <uint32_t kPort>
class Omap3530McBsp : public Omap3530PrcmStubBlock {
public:
    using Omap3530PrcmStubBlock::Omap3530PrcmStubBlock;
    using Clock       = Omap3530McBspPortClock<kPort>;
    using Transmitter = Omap3530McBspTransmitter<kPort>;
    using Receiver    = Omap3530McBspReceiver<kPort>;

    static constexpr uint32_t kThreshMask = kPort == 2u ? 0x7FFu : 0x7Fu;

    uint32_t MmioBase() const override { return BaseOf(kPort); }
    uint32_t MmioSize() const override { return 0x00001000u; }

    void OnReady() override {
        cycles_ = &emu_.Get<GuestCycleClock>();
        clock_  = &emu_.Get<Clock>();
        tx_     = &emu_.Get<Transmitter>();
        rx_     = &emu_.Get<Receiver>();
        Omap3530PrcmStubBlock::OnReady();
        LoadResetValues();
        ApplyRegs();
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) { ResetModule(); });
    }

    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - MmioBase();
        switch (off) {
        case kOffDrr:       return rx_->ReadData();
        case kOffDxr:       return dxr_;
        case kOffSpcr2:     return PeekReg(off) | TxStatus();
        case kOffSpcr1:     return PeekReg(off) | RxStatus();
        case kOffXbuffstat: return tx_->FreeLocations();
        case kOffRbuffstat: return rx_->Occupied();
        case kOffRev:
        case kOffRintclr:
        case kOffXintclr:
        case kOffRovflclr:
        case kOffStatus:    return 0u;
        }
        if (StoredMask(off, kThreshMask) != 0u) return PeekReg(off);
        emu_.Get<Fatal>().Die("%s: read at offset 0x%03X; not modelled", Label(), off);
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - MmioBase();
        if (off == kOffDxr) {
            dxr_ = value;
            tx_->WriteData(value);
            return;
        }
        if (off == kOffRintclr || off == kOffXintclr || off == kOffRovflclr || off == kOffIrqstatus) return;
        if (off == kOffSysconfig && (value & kSysconfigSoftReset) != 0u) {
            ResetModule();
            return;
        }
        const uint32_t mask = StoredMask(off, kThreshMask);
        if (mask == 0u) {
            emu_.Get<Fatal>().Die("%s: write 0x%08X at offset 0x%03X; not modelled", Label(), value, off);
        }
        RequireModelledWrite(off, value & mask);
        {
            std::lock_guard<std::mutex> lk(mu_);
            const Portions before = InReset(regs_[kOffSpcr1 / 4u], regs_[kOffSpcr2 / 4u]);
            regs_[off / 4u]       = value & mask;
            const Portions after  = InReset(regs_[kOffSpcr1 / 4u], regs_[kOffSpcr2 / 4u]);
            eff_ = Latch(eff_, StoredLocked(),
                         {before.tx || after.tx, before.rx || after.rx, before.clkg || after.clkg,
                          before.fsg || after.fsg});
        }
        ApplyRegs();
    }

    /* SPRUF98Y section 21.6.1 CAUTION (page 3054): DXR_REG and DRR_REG support 8/16/32-bit accesses;
       section 21.4.2.1 (page 2992): "when a byte is not enabled, the byte value in the memory will
       contain the previous written value". */
    void WriteByte(uint32_t addr, uint8_t value) override {
        if (((addr - MmioBase()) & ~3u) != kOffDxr) {
            emu_.Get<Fatal>().Die("%s: 8-bit write 0x%02X at 0x%08X; not modelled", Label(), value, addr);
        }
        WritePartialDxr(addr, value, 0xFFu);
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        const uint32_t off = HalfOffset("WriteHalf (misaligned)", addr, value) & ~3u;
        if (off == kOffDxr) {
            WritePartialDxr(addr, value, 0xFFFFu);
            return;
        }
        WriteWord(MmioBase() + off, WordWithHalf(addr, value));
    }
    uint8_t ReadByte(uint32_t addr) override {
        const uint32_t off = addr - MmioBase();
        if ((off & ~3u) != kOffDrr) emu_.Get<Fatal>().Die("%s: 8-bit read at 0x%08X; not modelled", Label(), addr);
        return static_cast<uint8_t>(rx_->ReadData() >> LaneShift(off));
    }
    uint16_t ReadHalf(uint32_t addr) override {
        const uint32_t off = HalfOffset("ReadHalf (misaligned)", addr, 0);
        return HalfOf(ReadWord(MmioBase() + (off & ~3u)), off);
    }

    void SaveState(StateWriter& w) override {
        Omap3530PrcmStubBlock::SaveState(w);
        w.Write<uint32_t>("mcbsp_dxr", dxr_);
        {
            std::lock_guard<std::mutex> lk(mu_);
            StateWriteField field(w);
            VisitRegs(eff_, field);
        }
        clock_->Save(w);
        tx_->Save(w);
        rx_->Save(w);
    }
    void RestoreState(StateReader& r) override {
        Omap3530PrcmStubBlock::RestoreState(r);
        r.Read("mcbsp_dxr", dxr_);
        {
            std::lock_guard<std::mutex> lk(mu_);
            StateReadField field(r);
            VisitRegs(eff_, field);
        }
        clock_->Restore(r);
        tx_->Restore(r);
        rx_->Restore(r);
    }
    void PostRestore() override {
        const Omap3530McBspRegs s = Snapshot();
        clock_->PostRestore(s);
        tx_->PostRestore(s);
        rx_->PostRestore(s);
    }

protected:
    const char* Label() const override { return label_; }
    const char* RegisterName(uint32_t) const override { return nullptr; }

private:
    void ApplyRegs() {
        const uint64_t now = cycles_->Cycles();
        tx_->OnTimingWillChange(now);
        rx_->OnTimingWillChange(now);
        const Omap3530McBspRegs s       = Snapshot();
        const auto              changes = clock_->Apply(s, now);
        tx_->Apply(s, changes.tx, now);
        rx_->Apply(s, changes.rx, now);
    }

    void WritePartialDxr(uint32_t addr, uint32_t value, uint32_t lane_mask) {
        const uint32_t shift = LaneShift(addr - MmioBase());
        const uint32_t mask  = lane_mask << shift;
        dxr_ = (dxr_ & ~mask) | ((value << shift) & mask);
        tx_->WriteData(dxr_);
    }

    uint32_t TxStatus() {
        if (!tx_->Enabled()) return 0u;
        return (tx_->Ready() ? kSpcrRdy : 0u) | (tx_->ShiftRegisterLoaded() ? kSpcrEmptyFull : 0u);
    }

    uint32_t RxStatus() {
        if (!rx_->Enabled()) return 0u;
        return (rx_->Ready() ? kSpcrRdy : 0u) | (rx_->Overrun() ? kSpcrEmptyFull : 0u);
    }

    Omap3530McBspRegs Snapshot() const {
        std::lock_guard<std::mutex> lk(mu_);
        return eff_;
    }

    Omap3530McBspRegs StoredLocked() const {
        Omap3530McBspRegs s;
        s.spcr2      = regs_[kOffSpcr2 / 4u];
        s.spcr1      = regs_[kOffSpcr1 / 4u];
        s.pcr        = regs_[kOffPcr / 4u];
        s.srgr1      = regs_[kOffSrgr1 / 4u];
        s.srgr2      = regs_[kOffSrgr2 / 4u];
        s.thrsh2     = regs_[kOffThrsh2 / 4u];
        s.thrsh1     = regs_[kOffThrsh1 / 4u];
        s.xccr       = regs_[kOffXccr / 4u];
        s.rccr       = regs_[kOffRccr / 4u];
        s.frame.xcr1 = regs_[kOffXcr1 / 4u];
        s.frame.xcr2 = regs_[kOffXcr2 / 4u];
        s.frame.rcr1 = regs_[kOffRcr1 / 4u];
        s.frame.rcr2 = regs_[kOffRcr2 / 4u];
        s.frame.mcr1 = regs_[kOffMcr1 / 4u];
        s.frame.mcr2 = regs_[kOffMcr2 / 4u];
        for (uint32_t i = 0; i < 8u; ++i) {
            s.frame.xcer[i] = regs_[kXcerOff[i] / 4u];
            s.frame.rcer[i] = regs_[kRcerOff[i] / 4u];
        }
        return s;
    }

    /* SPRUF98Y Table 21-34 (page 3050): the transmit pins are general-purpose I/O with XRST 0 and XIOEN 1,
       the receive pins with RRST 0 and RIOEN 1; Table 21-50 RJUST 3 and Table 21-124 RCCR (page 3105). */
    void RequireModelledWrite(uint32_t off, uint32_t value) {
        auto& fatal = emu_.Get<Fatal>();
        if (off == kOffIrqenable && value != 0u) {
            fatal.Die("%s: IRQENABLE 0x%08X; the interrupt lines are not modelled", Label(), value);
        }
        if (off == kOffSselcr && (value & kSselcrSidetoneen) != 0u) {
            fatal.Die("%s: SSELCR 0x%08X enables the sidetone; not modelled", Label(), value);
        }
        if (off == kOffXccr && (value & kXccrUnmodelled) != 0u) {
            fatal.Die("%s: XCCR 0x%08X sets EXTCLKGATE, XFULL_CYCLE, DLB or XDISABLE; not modelled", Label(),
                      value);
        }
        if (off == kOffRccr && ((value & mb::kRccrRdisable) != 0u || (value & mb::kRccrRfullCycle) == 0u)) {
            fatal.Die("%s: RCCR 0x%08X sets RDISABLE or the receive half-cycle mode; not modelled", Label(),
                      value);
        }
        const uint32_t spcr1 = off == kOffSpcr1 ? value : PeekReg(kOffSpcr1);
        const uint32_t spcr2 = off == kOffSpcr2 ? value : PeekReg(kOffSpcr2);
        const uint32_t pcr   = off == kOffPcr ? value : PeekReg(kOffPcr);
        if (((spcr2 & mb::kSpcr2Xrst) == 0u && (pcr & mb::kPcrXioen) != 0u) ||
            ((spcr1 & mb::kSpcr1Rrst) == 0u && (pcr & mb::kPcrRioen) != 0u)) {
            fatal.Die("%s: PCR 0x%08X with SPCR1 0x%08X / SPCR2 0x%08X selects general-purpose I/O on the "
                      "McBSP pins; not modelled", Label(), pcr, spcr1, spcr2);
        }
        if (off != kOffSpcr1 && off != kOffSpcr2) return;
        if ((value & kSpcrSyncErr) != 0u) {
            fatal.Die("%s: SPCR at 0x%03X written 0x%08X sets the sync error bit; not modelled", Label(), off,
                      value);
        }
        const uint32_t intm = mb::IntMode(value);
        if (intm == 1u || intm == 2u) {
            fatal.Die("%s: SPCR at 0x%03X written 0x%08X selects the end-of-frame or frame-sync interrupt; "
                      "not modelled", Label(), off, value);
        }
        if (off == kOffSpcr1 && mb::Rjust(value) == 3u) {
            fatal.Die("%s: SPCR1 0x%08X selects the reserved RJUST 3", Label(), value);
        }
    }

    /* SPRUF98Y Table 21-60 SRGR2 reset 0x2000, Table 21-62 SRGR1 reset 0x0001, Table 21-122 XCCR reset
       0x1008, Table 21-124 RCCR reset 0x0808, Table 21-130 SSELCR reset 0x0084. */
    void LoadResetValues() {
        std::lock_guard<std::mutex> lk(mu_);
        std::fill(regs_.begin(), regs_.end(), 0u);
        regs_[kOffSrgr2 / 4u]  = 0x2000u;
        regs_[kOffSrgr1 / 4u]  = 0x0001u;
        regs_[kOffXccr / 4u]   = 0x1008u;
        regs_[kOffRccr / 4u]   = 0x0808u;
        regs_[kOffSselcr / 4u] = 0x0084u;
        dxr_                   = 0u;
        eff_                   = StoredLocked();
    }

    void ResetModule() {
        LoadResetValues();
        tx_->Disable();
        rx_->Disable();
        ApplyRegs();
    }

    static constexpr char kLabels[5][7] = {"MCBSP1", "MCBSP2", "MCBSP3", "MCBSP4", "MCBSP5"};
    const char* const     label_        = kLabels[kPort - 1u];

    GuestCycleClock* cycles_ = nullptr;
    Clock*           clock_  = nullptr;
    Transmitter*     tx_     = nullptr;
    Receiver*        rx_     = nullptr;
    uint32_t         dxr_    = 0u;
    Omap3530McBspRegs eff_;
};

using Omap3530McBsp1 = Omap3530McBsp<1>;
using Omap3530McBsp2 = Omap3530McBsp<2>;
using Omap3530McBsp3 = Omap3530McBsp<3>;
using Omap3530McBsp4 = Omap3530McBsp<4>;
using Omap3530McBsp5 = Omap3530McBsp<5>;

}

REGISTER_SERVICE(Omap3530McBsp1);
REGISTER_SERVICE(Omap3530McBsp2);
REGISTER_SERVICE(Omap3530McBsp3);
REGISTER_SERVICE(Omap3530McBsp4);
REGISTER_SERVICE(Omap3530McBsp5);
