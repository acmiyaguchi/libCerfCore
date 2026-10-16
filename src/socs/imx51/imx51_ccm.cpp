#include "imx51_ccm.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include "../../execution/guest_cycle_clock.h"
#include "../../execution/engine_bridge.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/state/state_stream.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "imx51_ccm_dvfs.h"
#include "imx51_ccm_status.h"
#include "imx51_clock_input.h"
#include "imx51_cortex_a8_platform.h"
#include "imx51_dpll1.h"
#include "imx51_dpll2.h"
#include "imx51_dpll3.h"
#include "imx51_id.h"

#include <cstdint>

namespace {

/* MCIMX51RM Table 7-3 CCM memory map. */
constexpr uint32_t kCcr    = 0x00u;
constexpr uint32_t kCsr    = 0x08u;
constexpr uint32_t kCcsr   = 0x0Cu;
constexpr uint32_t kCacrr  = 0x10u;
constexpr uint32_t kCbcdr  = 0x14u;
constexpr uint32_t kCbcmr  = 0x18u;
constexpr uint32_t kCdcdr  = 0x30u;
constexpr uint32_t kCwdr   = 0x44u;
constexpr uint32_t kCdhipr = 0x48u;
constexpr uint32_t kCdcr   = 0x4Cu;
constexpr uint32_t kCtor   = 0x50u;
constexpr uint32_t kClpcr  = 0x54u;
constexpr uint32_t kCisr   = 0x58u;
constexpr uint32_t kCimr   = 0x5Cu;
constexpr uint32_t kCmeor  = 0x84u;
constexpr uint32_t kCcgr0  = 0x68u;
constexpr uint32_t kCcgr2  = 0x70u;
constexpr uint32_t kCcgr6  = 0x80u;

/* Table 7-10 CACRR arm_podf [2:0]. */
constexpr uint32_t kCacrrArmPodfMask = 0x7u;

struct CcmReset { uint32_t off; uint32_t val; };
constexpr CcmReset kResets[] = {
    /* CCR cosc_en [12] set: sync_2 SBOOT nk.exe sub_80051DBC 0x80051FD8 `tst r3, #0x1000`
       records the 24 MHz oscillator rate on it. */
    {0x00, 0x00001EFFu},
    {0x14, 0x19239145u}, {0x18, 0x000020C0u},
    {0x1C, 0xA6A2A020u}, {0x20, 0x02A5A88Au}, {0x24, 0x00C30318u},
    {0x28, 0x00860041u}, {0x2C, 0x00860041u}, {0x30, 0x04320DD2u},
    {0x38, 0x02090241u}, {0x3C, 0x00010241u}, {0x40, 0x00010241u},
    {0x54, 0x00000079u},
    {0x60, 0x000A0001u}, {0x64, 0x0000FE62u}, {0x68, 0xFFFFFFFFu},
    {0x6C, 0xFFFFFFFFu}, {0x70, 0xFFFFFFFFu}, {0x74, 0xFFFFFFFFu},
    {0x78, 0xFFFFFFFFu}, {0x7C, 0xFFFFFFFFu}, {0x80, 0xFFFFFFFFu},
    {0x84, 0xFFFFFFFFu},
};

/* Table 7-9 CCSR, Table 7-11 CBCDR, Table 7-12 CBCMR. */
constexpr uint32_t kCcsrPll3SwSel  = 1u << 0;
constexpr uint32_t kCcsrPll2SwSel  = 1u << 1;
constexpr uint32_t kCcsrPll1SwSel  = 1u << 2;
constexpr uint32_t kCcsrLpApm      = 1u << 9;
constexpr uint32_t kCbcdrPeriphSel = 1u << 25;
constexpr uint32_t kCbcmrPerIpg    = 1u << 0;
constexpr uint32_t kCbcmrPerLpApm  = 1u << 1;

/* Table 7-6 CCR: bits 31-13 read 0, FPM_MULT [11] "0 FPM multiply by 512, 1 FPM multiply by
   1024". */
constexpr uint32_t kCcrWritable = 0x00001FFFu;
constexpr uint32_t kCcrFpmMult  = 1u << 11;

/* Table 7-13 CSCMR1, Figure 7-13 CS1CDR, Figure 7-14 CS2CDR. */
constexpr uint32_t kCscmr1 = 0x1Cu;
constexpr uint32_t kCscdr1 = 0x24u;
constexpr uint32_t kCs1cdr = 0x28u;
constexpr uint32_t kCs2cdr = 0x2Cu;

/* Figure 7-15 CDCDR: bits 31 and 15 read 0; Figure 7-22 CTOR: bits 31-14 read 0; Figure 7-35
   CMEOR: bits 31-24, 11 and 1 read 1. */
constexpr uint32_t kCdcdrWritable = 0x7FFF7FFFu;
constexpr uint32_t kCtorWritable  = 0x00003FFFu;
constexpr uint32_t kCmeorWritable = 0x00FFF7FDu;

/* Figure 7-8 CBCDR fields and Table 7-27 CISR bits 25-16: dividers_loaded [16] covers axi_a_podf,
   axi_b_podf, emi_slow_podf, ahb_podf, nfc_podf, periph_clk_sel and emi_clk_sel. */
struct DividerLoad { uint32_t field; uint32_t cisr; bool group; };
constexpr DividerLoad kDividerLoads[] = {
    {0x7u << 16, 1u << 17, true},  {0x7u << 19, 1u << 18, true},  {0x7u << 22, 1u << 19, true},
    {0x7u << 10, 1u << 20, true},  {0x7u << 13, 1u << 21, true},  {1u << 25, 1u << 22, true},
    {1u << 26, 1u << 23, true},    {0x7u << 27, 1u << 24, false}, {1u << 30, 1u << 25, false},
};
constexpr uint32_t kCisrDividersLoaded = 1u << 16;

/* MCIMX51RM Table 7-3 (pages 7-5 / 7-6): CCR 73FD_4000 through CMEOR 73FD_4084; +0x34 is CHSCDR
   per the Linux i.MX5 CCM register map, MXC_CCM_CHSCDR. */
bool IsCcmRegister(uint32_t off) { return (off & 0x3u) == 0u && off <= kCmeor; }

}

bool Imx51Ccm::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Imx51;
}

void Imx51Ccm::OnReady() {
    input_    = &emu_.Get<Imx51ClockInput>();
    platform_ = &emu_.Get<Imx51CortexA8Platform>();
    status_   = &emu_.Get<Imx51CcmStatus>();
    dvfs_     = &emu_.Get<Imx51CcmDvfs>();
    dvfs_->SetRateListener([this] { ApplyRates(); });
    ResetRegisters();
    auto& reset = emu_.Get<GuestCpuReset>();
    reset.RegisterResetListener([this](ResetLineKind) { ResetRegisters(); });
    reset.RegisterResetReleaseListener([this] {
        ApplyRates();
        NotifyGates();
    });
    emu_.Get<PeripheralDispatcher>().Register(this);
    ApplyRates();
}

/* MCIMX51RM Table 54-10: every reset type asserts system_early_rst_b, which resets
   the CCM. */
void Imx51Ccm::ResetRegisters() {
    regs_.fill(0u);
    for (const auto& r : kResets) regs_[r.off >> 2] = r.val;
    dvfs_->Reset();
    status_->Reset(Reg(kCcr), Reg(kClpcr));
}

uint32_t Imx51Ccm::ReadWord(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    if (!IsCcmRegister(off)) HaltUnsupportedAccess("ReadWord", addr, 0);
    switch (off) {
        case kCsr:    return status_->Csr(Reg(kClpcr));
        case kCisr:   return status_->Cisr();
        case kCimr:   return status_->Cimr();
        case kCwdr:   return status_->Cwdr();
        case kCdcr:   return dvfs_->Cdcr();
        case kCdhipr: return dvfs_->Cdhipr();
        default:      return regs_[off >> 2];
    }
}

/* Table 7-3: CSR and CDHIPR are read-only, CISR is write-1-to-clear. */
void Imx51Ccm::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - MmioBase();
    if (!IsCcmRegister(off)) HaltUnsupportedAccess("WriteWord", addr, value);
    switch (off) {
        case kCsr:  case kCdhipr: return;
        case kCisr: status_->WriteCisr(value); return;
        case kCimr: status_->WriteCimr(value); return;
        case kCwdr: status_->WriteCwdr(value); return;
        case kCdcr: dvfs_->WriteCdcr(value); return;
        default:    break;
    }
    uint32_t stored = value;
    if (off == kCcr)   stored = value & kCcrWritable;
    if (off == kCdcdr) stored = value & kCdcdrWritable;
    if (off == kCtor)  stored = value & kCtorWritable;
    if (off == kCmeor) stored = (value & kCmeorWritable) | ~kCmeorWritable;
    const uint32_t old = regs_[off >> 2];
    regs_[off >> 2] = stored;
    if (off == kCcr || off == kClpcr) status_->OnControl(Reg(kCcr), Reg(kClpcr));
    if (off == kCbcdr) RaiseDividerLoads(old, stored);
    const bool timing = off == kCcr || off == kCcsr || off == kCbcdr || off == kCbcmr ||
                        off == kClpcr || off == kCcgr2 || off == kCscmr1 || off == kCscdr1 ||
                        off == kCs1cdr || off == kCs2cdr;
    if (off == kCacrr) {
        if (dvfs_->WriteArmPodf(stored & kCacrrArmPodfMask)) ApplyRates();
    } else if (timing && old != stored) {
        ApplyRates();
    }
    if (off >= kCcgr0 && off <= kCcgr6 && old != stored) NotifyGates();
}

/* MCIMX51RM Table 7-27 bits 25-16: ipi_int_1 "due to frequency change" or "due to update" of each
   field; page 7-46: the CDHIPR handshake dividers and muxes are the same fields. */
void Imx51Ccm::RaiseDividerLoads(uint32_t old, uint32_t now) {
    uint32_t bits = 0u;
    for (const auto& d : kDividerLoads) {
        if (((old ^ now) & d.field) == 0u) continue;
        bits |= d.cisr;
        if (d.group) bits |= kCisrDividersLoaded;
    }
    if (bits != 0u) status_->Raise(bits);
}

void Imx51Ccm::SaveState(StateWriter& w) {
    w.WriteBytes("regs", regs_.data(), sizeof(regs_));
    status_->SaveState(w);
    dvfs_->SaveState(w);
}

void Imx51Ccm::RestoreState(StateReader& r) {
    r.ReadBytes("regs", regs_.data(), sizeof(regs_));
    status_->RestoreState(r);
    dvfs_->RestoreState(r);
}

void Imx51Ccm::PostRestore() {
    status_->PostRestore();
    ApplyRates();
    NotifyGates();
}

void Imx51Ccm::RegisterRateListener(std::function<void()> fn) {
    listeners_.push_back(std::move(fn));
}

void Imx51Ccm::RegisterGateListener(std::function<void()> fn) {
    gate_listeners_.push_back(std::move(fn));
}

void Imx51Ccm::NotifyGates() {
    for (auto& fn : gate_listeners_) fn();
}

void Imx51Ccm::ApplyRates() {
    emu_.Get<Imx51Dpll1>().OnInputChange();
    emu_.Get<Imx51Dpll2>().OnInputChange();
    emu_.Get<Imx51Dpll3>().OnInputChange();
    auto&      clock = emu_.Get<GuestCycleClock>();
    const Rate arm   = ArmClkRate();
    const bool stopped = arm.num == 0u;
    if (!stopped) clock.SetClockRate(arm);
    emu_.Get<EngineBridge>().Guest().SetCoreClockStopped(stopped);
    if (stopped != core_stopped_) {
        LOG(SocClkpwr, "Imx51Ccm: ARM clock root %s (CCSR %08X CACRR %08X)\n",
            stopped ? "0 Hz, core stopped" : "back, core running", Reg(kCcsr), Reg(kCacrr));
        core_stopped_ = stopped;
    }
    const Rate rate = clock.ClockRate();
    if (rate != applied_) {
        LOG(SocClkpwr, "Imx51Ccm: ARM clock %llu/%llu -> %llu/%llu Hz (CCSR %08X CACRR %08X "
            "DPLL1 %s)\n", static_cast<unsigned long long>(applied_.num),
            static_cast<unsigned long long>(applied_.den),
            static_cast<unsigned long long>(rate.num), static_cast<unsigned long long>(rate.den),
            Reg(kCcsr), Reg(kCacrr),
            emu_.Get<Imx51Dpll1>().Restarted() ? "restarted" : "boot ROM state");
        applied_ = rate;
    }
    for (auto& fn : listeners_) fn();
}

/* Figure 7-39: ARM_CLK_ROOT is pll1_sw_clk through the arm_podf divider, value + 1. */
GuestCycleClock::Rate Imx51Ccm::ArmClkRate() const {
    return Pll1SwClkRate().Scaled(1u, uint64_t{dvfs_->ArmPodf()} + 1u);
}

GuestCycleClock::Rate Imx51Ccm::FpmRate() const {
    if (!status_->FpmOn()) return Rate{0u, 1u};
    return Rate{input_->CkilHz() * ((Reg(kCcr) & kCcrFpmMult) != 0u ? 1024u : 512u), 1u};
}

/* Table 7-9 lp_apm [9]: 0 on-chip oscillator clock output, 1 FPM clock output. */
GuestCycleClock::Rate Imx51Ccm::LpApmRate() const {
    if ((Reg(kCcsr) & kCcsrLpApm) != 0u) return FpmRate();
    return status_->OscillatorRate();
}

GuestCycleClock::Rate Imx51Ccm::BypassClkRate() const {
    return emu_.Get<Imx51Dpll1>().ReferenceRate();
}

/* Table 7-9 step_sel [8:7]: 00 lp_apm, 01 pll1 bypass clock, 10 pll2 divided by
   pll2_div_podf [6:5] + 1, 11 pll3 divided by pll3_div_podf [4:3] + 1. */
GuestCycleClock::Rate Imx51Ccm::StepClkRate() const {
    const uint32_t ccsr = Reg(kCcsr);
    switch ((ccsr >> 7) & 0x3u) {
        case 0u: return LpApmRate();
        case 1u: return BypassClkRate();
        case 2u: return emu_.Get<Imx51Dpll2>().OutputRate().Scaled(1u, ((ccsr >> 5) & 0x3u) + 1u);
        default: return emu_.Get<Imx51Dpll3>().OutputRate().Scaled(1u, ((ccsr >> 3) & 0x3u) + 1u);
    }
}

/* Table 7-9 pll1_sw_clk_sel [2]: 0 pll1_main_clk, 1 step_clk. */
GuestCycleClock::Rate Imx51Ccm::Pll1SwClkRate() const {
    if ((Reg(kCcsr) & kCcsrPll1SwSel) != 0u) return StepClkRate();
    return emu_.Get<Imx51Dpll1>().OutputRate();
}

/* Table 7-9 pll2_sw_clk_sel [1] / pll3_sw_clk_sel [0]: 1 selects the bypass clock. */
GuestCycleClock::Rate Imx51Ccm::Pll2SwClkRate() const {
    if ((Reg(kCcsr) & kCcsrPll2SwSel) != 0u) return BypassClkRate();
    return emu_.Get<Imx51Dpll2>().OutputRate();
}

GuestCycleClock::Rate Imx51Ccm::Pll3SwClkRate() const {
    if ((Reg(kCcsr) & kCcsrPll3SwSel) != 0u) return BypassClkRate();
    return emu_.Get<Imx51Dpll3>().OutputRate();
}

/* Table 7-13 uart_clk_sel [25:24], esdhc2_clk_sel [17:16], ecspi_clk_sel [5:4]: 00 pll1_sw_clk,
   01 pll2_sw_clk, 10 pll3_sw_clk, 11 lp_apm clock. */
GuestCycleClock::Rate Imx51Ccm::SwClkSelectRate(uint32_t sel) const {
    switch (sel) {
        case 0u: return Pll1SwClkRate();
        case 1u: return Pll2SwClkRate();
        case 2u: return Pll3SwClkRate();
        default: return LpApmRate();
    }
}

/* Table 7-12 periph_apm_sel [13:12]: 00 pll1_sw_clk, 01 pll3_sw_clk, 10 lp_apm. */
GuestCycleClock::Rate Imx51Ccm::PeriphApmClkRate() const {
    switch ((Reg(kCbcmr) >> 12) & 0x3u) {
        case 0u: return Pll1SwClkRate();
        case 1u: return Pll3SwClkRate();
        case 2u: return LpApmRate();
        default: return Rate{0u, 1u};
    }
}

/* Table 7-13 ssi_apm_clk_sel [9:8]: 00 CAMP1 of CKIH, 01 LP-APM clock selector output,
   10 CAMP2 of CKIH2. */
GuestCycleClock::Rate Imx51Ccm::SsiLpApmRate() const {
    switch ((Reg(kCscmr1) >> 8) & 0x3u) {
        case 0u: return Rate{status_->Camp1On() ? input_->CkihHz() : 0u, 1u};
        case 1u: return LpApmRate();
        case 2u: return Rate{status_->Camp2On() ? input_->Ckih2Hz() : 0u, 1u};
        default: return Rate{0u, 1u};
    }
}

/* Table 7-13 ssi1_clk_sel [15:14] / ssi2_clk_sel [13:12]: 00 pll1_sw_clk, 01 pll2_sw_clk,
   10 pll3_sw_clk, 11 ssi_lp_apm_clk; ssi3_clk_sel [11]: 0 ssi1_clk_root, 1 ssi2_clk_root.
   Table 7-16 / Figure 7-14: pred [8:6] and podf [5:0], each a divide by value + 1. */
GuestCycleClock::Rate Imx51Ccm::SsiClockRate(uint32_t ssi) const {
    const uint32_t cscmr1 = Reg(kCscmr1);
    const uint32_t root   = ssi == 3u ? ((cscmr1 >> 11) & 0x1u) + 1u : ssi;
    const uint32_t cdr    = Reg(root == 1u ? kCs1cdr : kCs2cdr);
    const uint32_t sel    = (cscmr1 >> (root == 1u ? 14u : 12u)) & 0x3u;
    const Rate     src    = sel == 3u ? SsiLpApmRate() : SwClkSelectRate(sel);
    return src.Scaled(1u, uint64_t{((cdr >> 6) & 0x7u) + 1u} * ((cdr & 0x3Fu) + 1u));
}

/* Table 7-15 uart_clk_pred [5:3] and uart_clk_podf [2:0], each a divide by value + 1. */
GuestCycleClock::Rate Imx51Ccm::UartClockRootRate() const {
    const Rate     src    = SwClkSelectRate((Reg(kCscmr1) >> 24) & 0x3u);
    const uint32_t cscdr1 = Reg(kCscdr1);
    return src.Scaled(1u, uint64_t{((cscdr1 >> 3) & 0x7u) + 1u} * ((cscdr1 & 0x7u) + 1u));
}

bool Imx51Ccm::EcspiClockRootRuns() const {
    return SwClkSelectRate((Reg(kCscmr1) >> 4) & 0x3u).num != 0u;
}

bool Imx51Ccm::Esdhc2ClockRootRuns() const {
    return SwClkSelectRate((Reg(kCscmr1) >> 16) & 0x3u).num != 0u;
}

/* Table 7-11 periph_clk_sel [25]: 0 pll2_sw_clk, 1 periph_apm_clk. */
GuestCycleClock::Rate Imx51Ccm::PeriphClkRate() const {
    return (Reg(kCbcdr) & kCbcdrPeriphSel) != 0u ? PeriphApmClkRate() : Pll2SwClkRate();
}

/* Figure 7-40: main_bus_clk leaves the DVFS divider; §7.4.7.2 (page 7-103): it divides "the root
   of bus clocks (ahb, axi, ipg, int_mem, ddr_clk, nfc_clk)". */
GuestCycleClock::Rate Imx51Ccm::MainBusClkRate() const {
    return PeriphClkRate().Scaled(1u, dvfs_->BusDivider());
}

/* Table 7-11: ahb_podf [12:10] and ipg_podf [9:8], each value + 1. */
GuestCycleClock::Rate Imx51Ccm::IpgClkRate() const {
    const uint32_t cbcdr = Reg(kCbcdr);
    return MainBusClkRate().Scaled(
        1u, uint64_t{((cbcdr >> 10) & 0x7u) + 1u} * (((cbcdr >> 8) & 0x3u) + 1u));
}

/* Table 7-12 perclk_ipg_sel [0] and perclk_lp_apm_sel [1]; Table 7-11
   perclk_pred1 [7:6], perclk_pred2 [5:3], perclk_podf [2:0], each value + 1.
   Figure 7-40: perclk_lp_apm_sel 0 takes the periph_clk_sel output ahead of the DVFS divider. */
GuestCycleClock::Rate Imx51Ccm::PerclkRootRate() const {
    const uint32_t cbcmr = Reg(kCbcmr);
    if ((cbcmr & kCbcmrPerIpg) != 0u) return IpgClkRate();
    const Rate     src   = (cbcmr & kCbcmrPerLpApm) != 0u ? LpApmRate() : PeriphClkRate();
    const uint32_t cbcdr = Reg(kCbcdr);
    return src.Scaled(1u, uint64_t{((cbcdr >> 6) & 0x3u) + 1u} * (((cbcdr >> 3) & 0x7u) + 1u) *
                              ((cbcdr & 0x7u) + 1u));
}

/* Table 7-3: CCGR0..CCGR6 at 0x68..0x80; Table 7-32: two CG bits per clock. */
uint32_t Imx51Ccm::ClockGate(uint32_t ccgr, uint32_t index) const {
    return (Reg(kCcgr0 + 4u * ccgr) >> (2u * index)) & 0x3u;
}

/* Table 7-32 CG value 01: on in run mode, off in WAIT and STOP; 11: on in all
   modes except STOP; 10: reserved. */
bool Imx51Ccm::ClockRunsIn(uint32_t ccgr, uint32_t index, FreescaleLowPowerMode mode) const {
    const uint32_t cg = ClockGate(ccgr, index);
    if (cg == 2u) return true;
    switch (mode) {
        case FreescaleLowPowerMode::kRun:  return cg != 0u;
        case FreescaleLowPowerMode::kWait: return cg == 3u;
        default:                           return false;
    }
}

/* Table 7-3: CLPCR at 0x54. 7.4.8.2.1 steps 7-9: the CCM leaves run mode only on
   ARM_DSM_REQUEST, which 14.4.3.4 LPC DSM blocks. */
uint32_t Imx51Ccm::WfiLowPowerMode() const {
    return platform_->DeepSleepRequestEnabled() ? (Reg(kClpcr) & 0x3u) : 0u;
}

REGISTER_SERVICE(Imx51Ccm);
