#include "imx51_dpll.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/state/state_stream.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "imx51_ccm.h"
#include "imx51_ccm_status.h"
#include "imx51_clock_input.h"
#include "imx51_id.h"

#include <cstdint>

namespace {

/* MCIMX51RM Table 22-1 register summary and Table 22-2 DP_CTL. */
constexpr uint32_t kDpCtlOff    = 0x000u;
constexpr uint32_t kDpConfigOff = 0x004u;
constexpr uint32_t kDpOpOff     = 0x008u;
constexpr uint32_t kDpMfdOff    = 0x00Cu;
constexpr uint32_t kDpMfnOff    = 0x010u;
constexpr uint32_t kDpHfsOpOff  = 0x01Cu;
constexpr uint32_t kDpHfsMfdOff = 0x020u;
constexpr uint32_t kDpTogcOff   = 0x028u;
constexpr uint32_t kDpDestatOff = 0x02Cu;

/* Figure 22-2: bit 11 is unimplemented; MUL_CTRL self-clears, HFSM and LRF are status bits.
   Figure 22-3: only AREN and LDREQ are implemented; Table 22-3: the DPLL acknowledge clears LDREQ. */
constexpr uint32_t kFieldMasks[] = {
    0x0000177Eu, 0x00000002u, 0x000000FFu, 0x03FFFFFFu, 0x07FFFFFFu, 0x07FFFFFFu,
    0x07FFFFFFu, 0x000000FFu, 0x03FFFFFFu, 0x07FFFFFFu, 0x0003FFFFu,
};

/* Table 22-10 TOG_DIS [17], TOG_EN [16]; Table 22-11: the desense logic runs when TOG_EN
   is set and TOG_DIS is clear; Figure 22-11: TOG_DIS resets to 1. */
constexpr uint32_t kTogcDis = 1u << 17;
constexpr uint32_t kTogcEn  = 1u << 16;

/* Figure 22-12: DP_DESTAT reset value; Table 22-12 TOG_SEL 0: desense logic inactive. */
constexpr uint32_t kDestatInactive = 0x00020000u;

constexpr uint32_t kCtlLrf         = 1u << 0;
constexpr uint32_t kCtlRst         = 1u << 4;
constexpr uint32_t kCtlUpen        = 1u << 5;
constexpr uint32_t kCtlPre         = 1u << 6;
constexpr uint32_t kCtlRefSelShift = 8;
constexpr uint32_t kCtlRefDiv      = 1u << 10;
constexpr uint32_t kCtlDpdck02En   = 1u << 12;
constexpr uint32_t kCtlMulCtrl     = 1u << 13;

constexpr uint32_t kCfgLdreq = 1u << 0;
constexpr uint32_t kCfgAren  = 1u << 1;

/* Table 22-2 REF_CLK_SEL: clk 0 and clk 1 "is not connected", "clk 2 is COSC (internal
   oscillator)", "clk 3 is FPM". */
constexpr uint32_t kRefSelCosc = 2u;
constexpr uint32_t kRefSelFpm  = 3u;

/* MCIMX51RM §7.4.6.5 (page 7-83): "initial value of UPEN should be asserted 1"; §7.4.5: with
   CLKSS = 0 "OSC output is used as reference clock for all three DPLL_IPs". */
constexpr uint32_t kCtlReset = kCtlUpen | (kRefSelCosc << kCtlRefSelShift);

constexpr uint32_t kMfnMask = 0x07FFFFFFu;
/* Table 22-5 (page 22-9) MFD[26:0]: "Note: Bit 26 is always 0." */
constexpr uint32_t kMfdMask = 0x03FFFFFFu;

}

bool Imx51Dpll::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Imx51;
}

void Imx51Dpll::OnReady() {
    ResetRegisters();
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
        ResetRegisters();
    });
    emu_.Get<PeripheralDispatcher>().Register(this);
}

/* MCIMX51RM Table 54-10: every reset type asserts system_early_rst_b, which resets the CCM and
   the PLL-IPs; §7.4.6.5 (page 7-83): the ignition releases reset once all three PLLs lock. */
void Imx51Dpll::ResetRegisters() {
    regs_.fill(0u);
    regs_[kDpCtlOff >> 2]    = kCtlReset;
    regs_[kDpConfigOff >> 2] = kCfgAren;
    regs_[kDpTogcOff >> 2]   = kTogcDis;
    port_         = Port{kCtlReset, 0u, 0u, 0u};
    input_        = BootReferenceRate();
    output_       = BootOutputRate();
    target_       = output_;
    lock_         = Lock::kLocked;
    restarted_    = false;
    load_wanted_  = false;
}

/* Table 22-13: dpllip_cpen is 0 with UPEN clear; Table 22-2 PRE "Asserts a hard reset to the
   DPLL". */
bool Imx51Dpll::Running() const {
    const uint32_t ctl = regs_[kDpCtlOff >> 2];
    return (ctl & kCtlUpen) != 0u && (ctl & kCtlPre) == 0u;
}

/* Table 22-2 LRF: a sticky bit, reset 0; a DP_CTL, DP_OP, DP_MFD, DP_HFS_OP or DP_HFS_MFD
   write, a hard reset or a DPLL enable breaks the lock. HFSM 0: LFS mode. */
uint32_t Imx51Dpll::ReadWord(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    if ((off & 0x3u) != 0u || off > kDpDestatOff) HaltUnsupportedAccess("ReadWord", addr, 0);
    if (off == kDpDestatOff) return kDestatInactive;
    uint32_t v = regs_[off >> 2];
    if (off == kDpCtlOff && lock_ == Lock::kLocked) v |= kCtlLrf;
    if (off == kDpConfigOff && load_wanted_) v |= kCfgLdreq;
    return v;
}

/* Table 22-3 AREN: a write to DP_CTL, DP_OP or DP_MFD issues a restart; otherwise DP_CTL RST
   restarts. */
void Imx51Dpll::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - MmioBase();
    if ((off & 0x3u) != 0u || off > kDpDestatOff) HaltUnsupportedAccess("WriteWord", addr, value);
    if (off == kDpDestatOff) return;
    if (off == kDpTogcOff && (value & kTogcEn) != 0u && (value & kTogcDis) == 0u) {
        emu_.Get<Fatal>().Die("Imx51Dpll %08X: DP_MFN_TOGC 0x%08X turns the desense logic on",
                              MmioBase(), value);
    }
    if (off == kDpCtlOff) LOG(SocClkpwr, "Imx51Dpll %08X: DP_CTL <- 0x%08X\n", MmioBase(), value);
    if (off == kDpCtlOff && (value & kCtlMulCtrl) != 0u) {
        emu_.Get<Fatal>().Die("Imx51Dpll %08X: DP_CTL write 0x%08X sets MUL_CTRL", MmioBase(),
                              value);
    }
    const bool was_running = Running();
    regs_[off >> 2] = value & kFieldMasks[off >> 2];
    if (off == kDpConfigOff) {
        if ((value & kCfgLdreq) != 0u && !load_wanted_) {
            load_wanted_ = true;
            ScheduleLoad();
        }
        return;
    }
    if (off == kDpTogcOff) return;
    const bool hfs    = off == kDpHfsOpOff || off == kDpHfsMfdOff;
    const bool breaks = off == kDpCtlOff || off == kDpOpOff || off == kDpMfdOff || hfs;
    if (!breaks) return;
    if (!Running()) {
        if (was_running) StopOutput();
        return;
    }
    const bool aren    = (regs_[kDpConfigOff >> 2] & kCfgAren) != 0u;
    const bool by_rst  = off == kDpCtlOff && (value & kCtlRst) != 0u;
    const bool restart = by_rst || (aren && !hfs);
    if (restart || !was_running) {
        Restart();
    } else if (lock_ == Lock::kLocked) {
        lock_ = Lock::kBroken;
    }
}

/* §22.3.3.1: the restart sends the control bits to the DPLL; Figure 22-15: the port updates
   with the restart. */
void Imx51Dpll::Restart() {
    port_ = Port{regs_[kDpCtlOff >> 2], regs_[kDpOpOff >> 2], regs_[kDpMfdOff >> 2],
                 regs_[kDpMfnOff >> 2]};
    FollowReference();
    target_    = ComputeRate();
    lock_      = Lock::kRelocking;
    restarted_ = true;
    LOG(SocClkpwr, "Imx51Dpll %08X: restart at %llu/%llu Hz\n", MmioBase(),
        static_cast<unsigned long long>(target_.num), static_cast<unsigned long long>(target_.den));
    if (SelectedInputRate().num != 0u) {
        OnLock();
        return;
    }
    emu_.Get<Imx51Ccm>().ApplyRates();
}

void Imx51Dpll::FollowReference() {
    input_ = SelectedInputRate();
}

void Imx51Dpll::OnInputChange() {
    const Rate input = SelectedInputRate();
    if (input == input_) return;
    FollowReference();
    if (!Running() || lock_ == Lock::kOff) return;
    target_ = ComputeRate();
    if (lock_ != Lock::kRelocking) {
        SetOutput(target_);
        return;
    }
    if (input.num != 0u) OnLock();
}

void Imx51Dpll::StopOutput() {
    lock_ = Lock::kOff;
    SetOutput(Rate{0u, 1u});
}

/* MCIMX51RM Table 7-27 bits 2-0 lrf_pll3 / lrf_pll2 / lrf_pll1: "Interrupt ipi_int_2 generation
   due to lock of pll1". */
void Imx51Dpll::OnLock() {
    lock_   = Lock::kLocked;
    output_ = target_;
    LOG(SocClkpwr, "Imx51Dpll %08X: locked at %llu/%llu Hz\n", MmioBase(),
        static_cast<unsigned long long>(output_.num), static_cast<unsigned long long>(output_.den));
    emu_.Get<Imx51CcmStatus>().Raise(LockInterruptBit());
    emu_.Get<Imx51Ccm>().ApplyRates();
    if (load_wanted_) ScheduleLoad();
}

/* §22.3.3.2: "The MFN bits (in register DP_MFN) are the only bits in the DPLL that can be changed
   after the DPLL was locked without restart"; LDREQ "will remain at '1' until a load_ack pulse is
   received". */
void Imx51Dpll::ScheduleLoad() {
    if (!Running() || lock_ == Lock::kOff) return;
    load_wanted_ = false;
    OnLoadAck();
}

void Imx51Dpll::OnLoadAck() {
    port_.mfn = regs_[kDpMfnOff >> 2];
    target_   = ComputeRate();
    if (lock_ != Lock::kRelocking) SetOutput(target_);
}

void Imx51Dpll::SetOutput(Rate rate) {
    const bool changed = rate != output_;
    output_ = rate;
    LOG(SocClkpwr, "Imx51Dpll %08X: output %llu/%llu Hz\n", MmioBase(),
        static_cast<unsigned long long>(rate.num), static_cast<unsigned long long>(rate.den));
    if (changed) emu_.Get<Imx51Ccm>().ApplyRates();
}

GuestCycleClock::Rate Imx51Dpll::BootReferenceRate() const {
    return Rate{emu_.Get<Imx51ClockInput>().OscHz(), 1u};
}

GuestCycleClock::Rate Imx51Dpll::SelectedInputRate() const {
    switch ((port_.ctl >> kCtlRefSelShift) & 0x3u) {
        case kRefSelCosc: return emu_.Get<Imx51CcmStatus>().OscillatorRate();
        case kRefSelFpm:  return emu_.Get<Imx51Ccm>().FpmRate();
        default:          return Rate{0u, 1u};
    }
}

/* Figure 22-13: the selected clock, divided by 2 when REF_CLK_DIV is set. */
GuestCycleClock::Rate Imx51Dpll::ReferenceRate() const {
    const Rate in = SelectedInputRate();
    return (port_.ctl & kCtlRefDiv) != 0u ? in.Scaled(1u, 2u) : in;
}

/* Eqn 22-1 with MF = MFI + MFN / (MFD + 1); Table 22-4 MFI below 5 reads as 5.
   Linux i.MX PLLv2 clock driver __clk_pllv2_recalc_rate: 2 x the reference,
   doubled again with DPDCK0_2_EN, MFN sign-extended from bit 26. */
GuestCycleClock::Rate Imx51Dpll::ComputeRate() const {
    int64_t mfi = (port_.op >> 4) & 0xFu;
    if (mfi < 5) mfi = 5;
    const int64_t pdf = static_cast<int64_t>(port_.op & 0xFu) + 1;
    const int64_t mfd = static_cast<int64_t>(port_.mfd & kMfdMask) + 1;
    int64_t mfn = static_cast<int64_t>(port_.mfn & kMfnMask);
    if ((mfn & 0x04000000) != 0) mfn -= 0x08000000;
    const int64_t mf = mfi * mfd + mfn;
    const Rate    ref = ReferenceRate();
    if (mf <= 0 || ref.num == 0u) return Rate{0u, 1u};
    uint64_t num = ref.num * ((port_.ctl & kCtlDpdck02En) != 0u ? 4u : 2u) *
                   static_cast<uint64_t>(mf);
    uint64_t den = ref.den * static_cast<uint64_t>(mfd) * static_cast<uint64_t>(pdf);
    return Rate{num, den}.Reduced();
}

void Imx51Dpll::SaveState(StateWriter& w) {
    w.WriteBytes("regs", regs_.data(), sizeof(regs_));
    w.Write<uint32_t>("port_ctl", port_.ctl);
    w.Write<uint32_t>("port_op", port_.op);
    w.Write<uint32_t>("port_mfd", port_.mfd);
    w.Write<uint32_t>("port_mfn", port_.mfn);
    w.Write<uint64_t>("output_num", output_.num);
    w.Write<uint64_t>("output_den", output_.den);
    w.Write<uint64_t>("target_num", target_.num);
    w.Write<uint64_t>("target_den", target_.den);
    w.Write<uint64_t>("input_num", input_.num);
    w.Write<uint64_t>("input_den", input_.den);
    w.Write<uint8_t>("lock_state", static_cast<uint8_t>(lock_));
    w.Write<uint8_t>("restarted", restarted_ ? 1u : 0u);
    w.Write<uint8_t>("load_wanted", load_wanted_ ? 1u : 0u);
}

void Imx51Dpll::RestoreState(StateReader& r) {
    r.ReadBytes("regs", regs_.data(), sizeof(regs_));
    r.Read("port_ctl", port_.ctl);
    r.Read("port_op", port_.op);
    r.Read("port_mfd", port_.mfd);
    r.Read("port_mfn", port_.mfn);
    r.Read("output_num", output_.num);
    r.Read("output_den", output_.den);
    r.Read("target_num", target_.num);
    r.Read("target_den", target_.den);
    r.Read("input_num", input_.num);
    r.Read("input_den", input_.den);
    uint8_t lock = 0, restarted = 0, wanted = 0;
    r.Read("lock_state", lock);
    r.Read("restarted", restarted);
    r.Read("load_wanted", wanted);
    lock_        = static_cast<Lock>(lock);
    restarted_   = restarted != 0u;
    load_wanted_ = wanted != 0u;
}
