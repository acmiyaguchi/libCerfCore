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

/* Section 10.8.26 CLKSEQ BYPASS_CPU [18]. */
constexpr uint32_t kBypassCpu       = 1u << 18;

/* Section 10.8.27 RESET: CHIP [1], DIG [0]. */
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

/* Sections 10.8.1 / 10.8.3 POWER note "Allow 10 us" and 10.8.2 / 10.8.4 "asserted after 50 us"
   (LOCK_COUNT reaches 0x4B0 = 1200 xtal cycles at 24 MHz): the lock completes at once on
   guest time, per timers_clocks.md "An operation that needs no chip speed does not run at chip
   speed" (a lock stays instant until a log shows a guest that fails). LOCK_COUNT therefore
   reads 0x4B0, the only count value the RM states, while locked, and 0 (its reset) while
   unlocked. FORCE_LOCK (10.8.2, "force pll lock sequence to start") restarts a sequence
   that is already instant, so it is stored and has no further effect. */
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

/* Section 10.3.1: integer mode divides the reference by DIV, "Divide by zero is NOT a valid
   programming value". Sections 10.3.2 / 10.3.3: with FRAC_EN set the DIV field is a binary
   fraction of 2^N (N = DIV field width), the output carrying DIV/2^N of the reference
   edges (8:3 example: 0.01100 -> 3/8; 4/3 gated example). */
GuestCycleClock::Rate Imx28Clkctrl::DividerRate(GuestCycleClock::Rate ref, uint32_t div,
                                                bool frac_en, uint32_t div_bits,
                                                const char* what) const {
    if (div == 0u) {
        emu_.Get<Fatal>().Die("Imx28Clkctrl: %s divider programmed to 0 (RM 10.3.1: not valid)",
                              what);
    }
    if (!frac_en) return ref.Scaled(1u, div).Reduced();
    return ref.Scaled(div, 1u << div_bits).Reduced();
}

/* Section 10.2.3.1: clk_p is ref_xtal through the 10-bit DIV_XTAL divider, or ref_cpu through
   the 6-bit DIV_CPU divider. Section 10.8.26: BYPASS_CPU 1 selects ref_xtal. Section 10.8.24:
   ref_cpu is PLL0 (10.8.1, 480 MHz) 480 * (18/CPUFRAC), gated by CLKGATECPU; section 10.6
   step 2-5: PLL on, then PFD gate cleared, before the bypass is switched off. */
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
        emu_.Get<Fatal>().Die("Imx28Clkctrl: BYPASS_CPU cleared while ref_cpu is not running "
                              "(PLL0CTRL0 %08X FRAC0 %08X); clk_p would stop (RM 10.6)",
                              regs_[kClkPll0Ctrl0], frac0);
    }
    if ((regs_[kClkPll0Ctrl0] & kPllDivSel) != 0u) {
        emu_.Get<Fatal>().Die("Imx28Clkctrl: PLL0CTRL0 DIV_SEL %u is a test mode 'currently NOT "
                              "supported' (RM 10.8.1)", (regs_[kClkPll0Ctrl0] >> 20) & 0x3u);
    }
    if (cpufrac < kFracMin || cpufrac > kFracMax) {
        emu_.Get<Fatal>().Die("Imx28Clkctrl: CPUFRAC %u outside 18-35 (RM 10.8.24)", cpufrac);
    }
    return DividerRate(GuestCycleClock::Rate{kPll0Hz * kFracNumerator, cpufrac},
                       cpu & kCpuDivCpuMask, false, 6u, "CPU DIV_CPU");
}

void Imx28Clkctrl::ApplyRates() {
    auto& clock = emu_.Get<GuestCycleClock>();
    clock.SetClockRate(CpuRate());
    const uint64_t hz = clock.CpuHz();
    if (hz != applied_hz_) {
        LOG(SocClkpwr, "Imx28Clkctrl: clk_p %llu -> %llu Hz (CLKSEQ %08X CPU %08X FRAC0 %08X)\n",
            static_cast<unsigned long long>(applied_hz_), static_cast<unsigned long long>(hz),
            regs_[kClkClkseq], regs_[kClkCpu], regs_[kClkFrac0]);
        applied_hz_ = hz;
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

/* Sections 10.8.24 / 10.8.25 note "can only be addressed by byte instructions". Stock ROM
   sh4-sdu nk.exe does word accesses to both: OAL_ClockSetupA (80209c34) word-writes FRAC0_CLR
   0x1B8 and FRAC0_SET 0x1B4 and word read-modify-writes FRAC0 0x1B0; OAL_ClockSetupB (80209cc4)
   word-writes FRAC1_CLR 0x1C8 / FRAC1_SET 0x1C4 and word read-modify-writes FRAC1 0x1C0. The
   shipped device runs that code, so all widths are accepted; the byte lane merge in WriteByte
   gives the byte-instruction result of the RM example. */
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

/* Sections 10.8.10-14, 10.8.17-20: "When [CLKGATE] is modified, or when it is high, the DIV
   field should not change its value. The DIV field can change ONLY when this clock gate bit
   field is low." Section 10.8.17/18: DIV_FRAC_EN "Always set to one (1) for functional
   operation", so a SAIF DIV is only programmed with it set. Section 10.8.7 SLOW_DIV [18:16]
   defines 0x0-0x5 only. Stock ROM sh4-sdu cspddk.dll DDKClockConfigBaud (c0603af0) follows the
   same order: gate cleared, FRAC_EN set, DIV written, gate restored. */
void Imx28Clkctrl::CheckDividerWrite(uint32_t index, uint32_t old, uint32_t next) {
    const uint32_t div = kImx28ClkctrlRegs[index].gated_div;
    if (div != 0u && ((old ^ next) & div) != 0u) {
        if (((old | next) & kClkGate) != 0u) {
            emu_.Get<Fatal>().Die("Imx28Clkctrl: reg %03X DIV changed with CLKGATE set or "
                                  "changing (%08X -> %08X); RM 10.8.10-20: DIV may change only "
                                  "when CLKGATE = 0", index << 4, old, next);
        }
        if ((index == kClkSaif0 || index == kClkSaif1) && (next & kSaifFracEn) == 0u) {
            emu_.Get<Fatal>().Die("Imx28Clkctrl: SAIF%u DIV programmed with DIV_FRAC_EN = 0 "
                                  "(%08X); RM 10.8.17/18: always one for functional operation",
                                  index == kClkSaif0 ? 0u : 1u, next);
        }
    }
    if (index == kClkHbus && ((next >> 16) & 0x7u) > 5u) {
        emu_.Get<Fatal>().Die("Imx28Clkctrl: HBUS SLOW_DIV %u undefined (RM 10.8.7: 0-5)",
                              (next >> 16) & 0x7u);
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
        /* Section 10.7: both soft-reset bits "reset themselves during a soft reset
           sequence", so they take their Reset row at reset delivery (LoadResetValues). A reset
           is requested only by a SET/write of a 1 bit (RM 39.3: SCT 1 bit performs the op,
           0 bit unaffected); CHIP reads 1 at reset (10.8.27), so the stored value alone is
           no request. Figure 11-3 labels DIG "warm boot" and CHIP "Reset DCDC and Digital";
           section 11.3.5: for both "The DC-DC converter and/or linear regulators continue to
           maintain the power supply rails during the reset", so DRAM keeps power and RAM
           survives: WarmReset (GuestCpuReset: RAM preserved). The cold path in Figure 11-3 is
           HW_POWER_RESET_PWD (power module, not here). The RM has no reset-cause register
           for these two bits (the only cause bits, HW_RTC_PERSISTENT0 THERMAL_RESET /
           EXTERNAL_RESET, are set on power-up after those events, 22.8.7), so no
           ResetCauseLatch is needed here. */
        const uint32_t req = op == Imx28SctOp::Clr ? 0u
                                                   : regs_[index] & value & (kResetChip | kResetDig);
        if (req != 0u) emu_.Get<GuestCpuReset>().WarmReset();
        return;
    }
    const bool rate = index == kClkCpu || index == kClkClkseq || index == kClkFrac0 ||
                      index == kClkPll0Ctrl0;
    if (rate && old != regs_[index]) ApplyRates();
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

uint8_t Imx28Clkctrl::ReadByte(uint32_t addr) {
    const uint32_t shift = (addr & 0x3u) * 8u;
    return static_cast<uint8_t>((ReadWord(addr & ~0x3u) >> shift) & 0xFFu);
}

uint16_t Imx28Clkctrl::ReadHalf(uint32_t addr) {
    if ((addr & 0x1u) != 0) HaltUnsupportedAccess("ReadHalf-unaligned", addr, 0);
    const uint32_t shift = (addr & 0x2u) * 8u;
    return static_cast<uint16_t>((ReadWord(addr & ~0x3u) >> shift) & 0xFFFFu);
}

/* An SCT address reads back 0, so a narrow write merges into 0 and only the written lane
   takes part (RM 39.3); a primary register merges into its current value. */
void Imx28Clkctrl::WriteByte(uint32_t addr, uint8_t value) {
    const uint32_t base  = addr & ~0x3u;
    const uint32_t shift = (addr & 0x3u) * 8u;
    const uint32_t old   = ReadWord(base);
    WriteWord(base, (old & ~(0xFFu << shift)) | (static_cast<uint32_t>(value) << shift));
}

void Imx28Clkctrl::WriteHalf(uint32_t addr, uint16_t value) {
    if ((addr & 0x1u) != 0) HaltUnsupportedAccess("WriteHalf-unaligned", addr, value);
    const uint32_t base  = addr & ~0x3u;
    const uint32_t shift = (addr & 0x2u) * 8u;
    const uint32_t old   = ReadWord(base);
    WriteWord(base, (old & ~(0xFFFFu << shift)) | (static_cast<uint32_t>(value) << shift));
}

REGISTER_SERVICE(Imx28Clkctrl);

}
