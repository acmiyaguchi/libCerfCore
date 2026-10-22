#include "imx28_clkctrl.h"

#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include "imx28_id.h"
#include "imx28_sct_reg.h"

#include <cstddef>

namespace cerfcore {

namespace {

/* MCIMX28RM Table 10-1 clk_xtal24m; section 10.8.1 PLL0 "480 MHz PLL0". */
constexpr uint64_t kXtalHz    = 24000000u;
constexpr uint64_t kPll0Hz    = 480000000u;

/* Section 10.8.1 PLL0CTRL0 POWER [17], DIV_SEL [21:20]; section 10.8.3 PLL1CTRL0 POWER [17]
   (and its note: PLL0 POWER ungates the xtal to all PLLs). */
constexpr uint32_t kPllPower  = 1u << 17;
constexpr uint32_t kPllDivSel = 0x3u << 20;

/* Section 10.8.2 / 10.8.4 PLLnCTRL1: LOCK [31], LOCK_COUNT [15:0] "will assert when the count
   reaches 0x4B0". */
constexpr uint32_t kPllLock      = 1u << 31;
constexpr uint32_t kPllLockCount = 0x4B0u;

/* Section 10.8.6 CPU: DIV_XTAL [25:16] / DIV_XTAL_FRAC_EN [26], DIV_CPU [5:0]. */
constexpr uint32_t kCpuDivXtalShift = 16;
constexpr uint32_t kCpuDivXtalMask  = 0x3FFu;
constexpr uint32_t kCpuDivXtalFrac  = 1u << 26;
constexpr uint32_t kCpuDivCpuMask   = 0x3Fu;

/* Section 10.8.24 FRAC0: CLKGATECPU [7], CPUFRAC [5:0], "480 * (18/CPUFRAC) where
   CPUFRAC = 18-35". */
constexpr uint32_t kFrac0GateCpu    = 1u << 7;
constexpr uint32_t kFracMask        = 0x3Fu;
constexpr uint32_t kFracMin         = 18u;
constexpr uint32_t kFracMax         = 35u;
constexpr uint64_t kFracNumerator   = 18u;

/* Section 10.8.8 XBUS: AUTO_CLEAR_DIV_ENABLE [11], DIV_FRAC_EN [10], DIV [9:0]. */
constexpr uint32_t kXbusAutoClear    = 1u << 11;
constexpr uint32_t kXbusDivMask      = 0x3FFu;
constexpr uint32_t kXbusDivFracEn    = 1u << 10;

/* Section 10.8.26 CLKSEQ BYPASS_CPU [18]. */
constexpr uint32_t kBypassCpu       = 1u << 18;

/* Section 10.8.27 RESET: WDOG_POR_DISABLE [5], CHIP [1], DIG [0]. */
constexpr uint32_t kResetWdogPorDisable = 1u << 5;
constexpr uint32_t kResetChip       = 1u << 1;
constexpr uint32_t kResetDig        = 1u << 0;

/* Section 10.8.21 ENET SLEEP [31], DISABLE [30], STATUS [29]. */
constexpr uint32_t kEnetSleep       = 1u << 31;
constexpr uint32_t kEnetDisable     = 1u << 30;
constexpr uint32_t kEnetStatus      = 1u << 29;

/* Section 10.8.23 FLEXCAN STOP_CAN0 [30] / CAN0_STATUS [29], STOP_CAN1 [28] / CAN1_STATUS [27]. */
constexpr uint32_t kCanStop0        = 1u << 30;
constexpr uint32_t kCanStatus0      = 1u << 29;
constexpr uint32_t kCanStop1        = 1u << 28;
constexpr uint32_t kCanStatus1      = 1u << 27;

/* Sections 10.8.24 / 10.8.25 bit diagrams: each 6-bit FRAC field has its STABLE bit just above
   it (FRAC shift + 6); "The value inverts when the new programmed fractional divide value
   has taken effect". */
struct StableField { uint32_t frac_shift; };
constexpr StableField kFrac0Fields[] = {{24u}, {16u}, {8u}, {0u}};
constexpr StableField kFrac1Fields[] = {{16u}, {8u}, {0u}};

/* Sections 10.8.10-20 CLKGATE [31]; 10.8.17/18 DIV_FRAC_EN [16]. */
constexpr uint32_t kClkGate   = 1u << 31;
constexpr uint32_t kSaifFracEn = 1u << 16;

constexpr uint32_t kMapEnd = 0x204u;

}  /* namespace */

bool Imx28Clkctrl::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Imx28;
}

void Imx28Clkctrl::OnReady() {
    LoadResetValues();
    auto& reset = emu_.Get<GuestCpuReset>();
    reset.RegisterResetListener([this](ResetLineKind) { LoadResetValues(); });
    reset.RegisterResetReleaseListener([this] { ApplyRates(); });
    emu_.Get<PeripheralDispatcher>().Register(this);
    ApplyRates();
}

void Imx28Clkctrl::LoadResetValues() {
    for (uint32_t i = 0; i < kImx28ClkctrlRegCount; ++i) {
        regs_[i] = kImx28ClkctrlRegs[i].reset & ~kImx28ClkctrlRegs[i].derived;
    }
}

/* Sections 10.8.1 / 10.8.3 POWER note "Allow 10 us"; 10.8.2 / 10.8.4 LOCK_COUNT 0x4B0,
   FORCE_LOCK. */
bool Imx28Clkctrl::PllLocked(uint32_t pll) const {
    const bool pll0_on = (regs_[kClkPll0Ctrl0] & kPllPower) != 0u;
    if (pll == 0u) return pll0_on;
    return pll0_on && (regs_[kClkPll1Ctrl0] & kPllPower) != 0u;
}

uint32_t Imx28Clkctrl::ComposeRead(uint32_t index) const {
    uint32_t v = regs_[index];
    switch (index) {
        case kClkPll0Ctrl1:
        case kClkPll1Ctrl1:
            if (PllLocked(index == kClkPll0Ctrl1 ? 0u : 1u)) v |= kPllLock | kPllLockCount;
            break;
        case kClkEnet:
            if ((v & (kEnetSleep | kEnetDisable)) != 0u) v |= kEnetStatus;
            break;
        case kClkFlexcan:
            if ((v & kCanStop0) != 0u) v |= kCanStatus0;
            if ((v & kCanStop1) != 0u) v |= kCanStatus1;
            break;
        default:
            break;
    }
    return v;
}

/* Section 10.3.1 "Divide by zero is NOT a valid programming value"; sections 10.3.2 / 10.3.3
   fractional divider, DIV/2^N of the reference edges. */
GuestCycleClock::Rate Imx28Clkctrl::DividerRate(GuestCycleClock::Rate ref, uint32_t div,
                                                bool frac_en, uint32_t div_bits,
                                                const char* what) const {
    if (div == 0u) {
        emu_.Get<Fatal>().Die("Imx28Clkctrl: %s divider programmed to 0", what);
    }
    if (!frac_en) return ref.Scaled(1u, div).Reduced();
    return ref.Scaled(div, 1u << div_bits).Reduced();
}

/* Section 10.2.3.1 clk_p; 10.8.26 BYPASS_CPU; 10.8.24 ref_cpu 480 * (18/CPUFRAC), CLKGATECPU;
   10.6 steps 2-5. */
GuestCycleClock::Rate Imx28Clkctrl::CpuRate() const {
    const uint32_t cpu = regs_[kClkCpu];
    if ((regs_[kClkClkseq] & kBypassCpu) != 0u) {
        return DividerRate(GuestCycleClock::Rate{kXtalHz, 1u},
                           (cpu >> kCpuDivXtalShift) & kCpuDivXtalMask,
                           (cpu & kCpuDivXtalFrac) != 0u, 10u, "CPU DIV_XTAL");
    }
    const uint32_t frac0 = regs_[kClkFrac0];
    const uint32_t cpufrac = frac0 & kFracMask;
    if ((regs_[kClkPll0Ctrl0] & kPllPower) == 0u || (frac0 & kFrac0GateCpu) != 0u) {
        emu_.Get<Fatal>().Die("Imx28Clkctrl: BYPASS_CPU cleared with ref_cpu stopped "
                              "(PLL0CTRL0 %08X FRAC0 %08X)", regs_[kClkPll0Ctrl0], frac0);
    }
    if ((regs_[kClkPll0Ctrl0] & kPllDivSel) != 0u) {
        emu_.Get<Fatal>().Die("Imx28Clkctrl: PLL0CTRL0 DIV_SEL %u",
                              (regs_[kClkPll0Ctrl0] >> 20) & 0x3u);
    }
    if (cpufrac < kFracMin || cpufrac > kFracMax) {
        emu_.Get<Fatal>().Die("Imx28Clkctrl: CPUFRAC %u", cpufrac);
    }
    return DividerRate(GuestCycleClock::Rate{kPll0Hz * kFracNumerator, cpufrac},
                       cpu & kCpuDivCpuMask, false, 6u, "CPU DIV_CPU");
}

/* Table 10-1: clk_x is ref_xtal through a 10-bit divider; section 10.8.8: "CLK_X is sourced
   from the 24-MHz XTAL through this divider. Do NOT divide by 0." */
GuestCycleClock::Rate Imx28Clkctrl::XbusRate() const {
    const uint32_t xbus = regs_[kClkXbus];
    return DividerRate(GuestCycleClock::Rate{kXtalHz, 1u}, xbus & kXbusDivMask,
                       (xbus & kXbusDivFracEn) != 0u, 10u, "XBUS DIV");
}

/* Section 35.2.1: saif_clk from a 16-bit fractional divider of the 480 MHz PLL reference;
   section 10.8.26 BYPASS_SAIF0 [0] / BYPASS_SAIF1 [1]; section 10.8.17 DIV_FRAC_EN always one. */
GuestCycleClock::Rate Imx28Clkctrl::SaifRate(uint32_t unit) const {
    const uint32_t reg = regs_[unit == 0u ? kClkSaif0 : kClkSaif1];
    const bool bypass = ((regs_[kClkClkseq] >> unit) & 1u) != 0u;
    if (bypass || (reg & kClkGate) != 0u || (reg & kSaifFracEn) == 0u ||
        (regs_[kClkPll0Ctrl0] & kPllPower) == 0u) {
        emu_.Get<Fatal>().Die("Imx28Clkctrl: SAIF%u clock used with SAIF %08X CLKSEQ %08X PLL0CTRL0 "
                              "%08X", unit, reg, regs_[kClkClkseq], regs_[kClkPll0Ctrl0]);
    }
    return DividerRate(GuestCycleClock::Rate{kPll0Hz, 1u}, reg & 0xFFFFu,
                       true, 16u, "SAIF DIV");
}

bool Imx28Clkctrl::WatchdogPorDisabled() const {
    return (regs_[kClkReset] & kResetWdogPorDisable) != 0u;
}

void Imx28Clkctrl::RegisterApbxListener(std::function<void()> fn) {
    apbx_listeners_.push_back(std::move(fn));
}

void Imx28Clkctrl::ApplyRates() {
    auto& clock = emu_.Get<GuestCycleClock>();
    clock.SetClockRate(CpuRate());
    const GuestCycleClock::Rate rate = clock.ClockRate();
    if (!(rate == applied_rate_)) {
        LOG(SocClkpwr, "Imx28Clkctrl: clk_p %llu/%llu -> %llu/%llu Hz (CLKSEQ %08X CPU %08X FRAC0 "
            "%08X)\n", static_cast<unsigned long long>(applied_rate_.num),
            static_cast<unsigned long long>(applied_rate_.den),
            static_cast<unsigned long long>(rate.num), static_cast<unsigned long long>(rate.den),
            regs_[kClkClkseq], regs_[kClkCpu], regs_[kClkFrac0]);
        applied_rate_ = rate;
    }
}

uint32_t Imx28Clkctrl::DecodeIndex(const char* op, uint32_t addr, uint32_t value,
                                   Imx28SctOp* sct_op) const {
    const uint32_t off = addr - MmioBase();
    if (off >= kMapEnd || (off & 0x3u) != 0u) HaltUnsupportedAccess(op, addr, value);
    const Imx28SctAccess a = Imx28DecodeSct(off);
    const uint32_t index = a.reg_offset >> 4;
    if (index >= kImx28ClkctrlRegCount || !kImx28ClkctrlRegs[index].present) {
        HaltUnsupportedAccess(op, addr, value);
    }
    if (a.op != Imx28SctOp::Write && !kImx28ClkctrlRegs[index].sct) {
        HaltUnsupportedAccess(op, addr, value);
    }
    *sct_op = a.op;
    return index;
}

/* Sections 10.8.24 / 10.8.25 "can only be addressed by byte instructions";
   sharp_brain_pw_sh4 nk.exe OAL_ClockSetupA 80209c34, OAL_ClockSetupB 80209cc4 (word accesses). */
uint32_t Imx28Clkctrl::ReadWord(uint32_t addr) {
    Imx28SctOp op;
    const uint32_t index = DecodeIndex("ReadWord", addr, 0, &op);
    if (op != Imx28SctOp::Write) return 0u;   /* RM 39.3: SCT registers read back 0 */
    return ComposeRead(index);
}

void Imx28Clkctrl::ToggleStableBits(uint32_t index, uint32_t old_value) {
    const StableField* f = index == kClkFrac0 ? kFrac0Fields : kFrac1Fields;
    const size_t n = index == kClkFrac0 ? sizeof(kFrac0Fields) / sizeof(kFrac0Fields[0])
                                        : sizeof(kFrac1Fields) / sizeof(kFrac1Fields[0]);
    for (size_t i = 0; i < n; ++i) {
        const uint32_t s = f[i].frac_shift;
        if (((old_value ^ regs_[index]) >> s & kFracMask) != 0u) regs_[index] ^= 1u << (s + 6u);
    }
}

/* Sections 10.8.10-14, 10.8.17-20 "The DIV field can change ONLY when this clock gate bit
   field is low"; 10.8.17/18 DIV_FRAC_EN "Always set to one (1)"; 10.8.7 SLOW_DIV [18:16];
   sharp_brain_pw_sh4 cspddk.dll DDKClockConfigBaud c0603af0. */
void Imx28Clkctrl::CheckDividerWrite(uint32_t index, uint32_t old, uint32_t next) {
    const uint32_t div = kImx28ClkctrlRegs[index].gated_div;
    if (div != 0u && ((old ^ next) & div) != 0u) {
        if (((old | next) & kClkGate) != 0u) {
            emu_.Get<Fatal>().Die("Imx28Clkctrl: reg %03X DIV changed with CLKGATE set or "
                                  "changing (%08X -> %08X)", index << 4, old, next);
        }
        if ((index == kClkSaif0 || index == kClkSaif1) && (next & kSaifFracEn) == 0u) {
            emu_.Get<Fatal>().Die("Imx28Clkctrl: SAIF%u DIV programmed with DIV_FRAC_EN 0 (%08X)",
                                  index == kClkSaif0 ? 0u : 1u, next);
        }
    }
    if (index == kClkXbus && (next & kXbusAutoClear) != 0u) {
        emu_.Get<Fatal>().Die("Imx28Clkctrl: XBUS %08X sets AUTO_CLEAR_DIV_ENABLE", next);
    }
    if (index == kClkHbus && ((next >> 16) & 0x7u) > 5u) {
        emu_.Get<Fatal>().Die("Imx28Clkctrl: HBUS SLOW_DIV %u", (next >> 16) & 0x7u);
    }
}

void Imx28Clkctrl::WriteRegister(uint32_t index, Imx28SctOp op, uint32_t value) {
    const Imx28ClkctrlReg& def = kImx28ClkctrlRegs[index];
    const uint32_t old = regs_[index];
    const uint32_t next = Imx28ApplySct(op, old, value);
    const uint32_t merged = (old & ~def.writable) | (next & def.writable);
    CheckDividerWrite(index, old, merged);
    regs_[index] = merged;
    if (index == kClkFrac0 || index == kClkFrac1) ToggleStableBits(index, old);
    if (index == kClkReset) {
        /* Section 10.7 soft reset sequence; Figure 11-3;
           section 11.3.5 "continue to maintain the power supply rails during the reset". */
        const uint32_t req = op == Imx28SctOp::Clr ? 0u
                                                   : regs_[index] & value & (kResetChip | kResetDig);
        if (req != 0u) {
            digital_only_reset_ = (req & kResetChip) == 0u;
            emu_.Get<GuestCpuReset>().WarmReset();
        }
        return;
    }
    const bool rate = index == kClkCpu || index == kClkClkseq || index == kClkFrac0 ||
                      index == kClkPll0Ctrl0;
    if (rate && old != regs_[index]) ApplyRates();
    const bool apbx = index == kClkXbus;
    if (apbx && old != regs_[index]) {
        for (auto& fn : apbx_listeners_) fn();
    }
}

void Imx28Clkctrl::WriteWord(uint32_t addr, uint32_t value) {
    Imx28SctOp op;
    const uint32_t index = DecodeIndex("WriteWord", addr, value, &op);
    if (kImx28ClkctrlRegs[index].writable == 0u) HaltUnsupportedAccess("WriteWord", addr, value);
    WriteRegister(index, op, value);
}

void Imx28Clkctrl::SaveState(StateWriter& w) {
    w.WriteBytes("regs", regs_, sizeof(regs_));
}

void Imx28Clkctrl::RestoreState(StateReader& r) {
    r.ReadBytes("regs", regs_, sizeof(regs_));
}

void Imx28Clkctrl::PostRestore() {
    ApplyRates();
}

REGISTER_SERVICE(Imx28Clkctrl);

}
