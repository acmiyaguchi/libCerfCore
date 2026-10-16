#include "omap3530_cm_clock_control.h"

#include <cerfcore/core/fatal.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "omap3530_board_clock_setup.h"
#include "omap3530_dpll.h"

namespace cerfcore {

namespace {

constexpr uint32_t kOffClkenPll    = 0x00u;
constexpr uint32_t kOffIdlestCkgen = 0x20u;
constexpr uint32_t kOffClksel1Pll  = 0x40u;
constexpr uint32_t kOffClksel2Pll  = 0x44u;
constexpr uint32_t kOffClksel3Pll  = 0x48u;

/* SPRUF98Y Table 4-178 (printed p. 466-467): CM_CLKEN_PLL [31:16] are the DPLL4
   fields - PWRDN_DSS1 [29], EN_PERIPH_DPLL_LPMODE [26], RESERVED [25:24] "Read
   returns 0", PERIPH_DPLL_FREQSEL [23:20], EN_PERIPH_DPLL [18:16], 0x7 lock. */
constexpr uint32_t kDpll4Fields    = 0xFFFF0000u;
constexpr uint32_t kDpll4Reserved  = 0x03000000u;
constexpr uint32_t kPwrdnDss1      = 1u << 29;
constexpr uint32_t kPwrdn96M       = 1u << 27;
constexpr uint32_t kLpMode         = 1u << 26;
constexpr uint32_t kFreqSelShift   = 20;
constexpr uint32_t kFreqSelMask    = 0xFu << kFreqSelShift;
constexpr uint32_t kEnShift        = 16;
constexpr uint32_t kEnMask         = 0x7u << kEnShift;
constexpr uint32_t kDpll4RateBits  = kPwrdnDss1 | kLpMode | kFreqSelMask | kEnMask;

/* Table 4-178 (printed p. 467-468): DPLL3 fields PWRDN_EMU_CORE [12], EN_CORE_DPLL_LPMODE [10],
   CORE_DPLL_FREQSEL [7:4], EN_CORE_DPLL_DRIFTGUARD [3], EN_CORE_DPLL [2:0] 0x5 low-power bypass,
   0x6 fast-relock bypass, 0x7 lock, "Other enums: Reserved"; [15:13], [11], [9:8] read 0. */
constexpr uint32_t kCoreWritable     = 0x000014FFu;
constexpr uint32_t kCoreFreqSelShift = 4;
constexpr uint32_t kCoreEnMask       = 0x7u;
constexpr uint32_t kEnLock           = 0x7u;

/* Table 4-192 (printed p. 475): PERIPH_DPLL_MULT [18:8], PERIPH_DPLL_DIV [6:0];
   [31:19] and [7] read 0. */
constexpr uint32_t kClksel2Mask = 0x0007FF7Fu;

/* Table 4-190 (printed p. 473-474): CORE_DPLL_CLKOUT_DIV [31:27] (0 reserved), CORE_DPLL_MULT [26:16],
   CORE_DPLL_DIV [14:8], SOURCE_96M [6] (1 = CM_SYS_CLK), SOURCE_54M [5], SOURCE_48M [3]. */
constexpr uint32_t kClksel1Mask = 0xFFFF7F68u;
constexpr uint32_t kSource96M   = 1u << 6;

/* Table 4-194 (printed p. 476): DIV_96M [4:0], 1 to 16, "Other enums: Reserved". */
constexpr uint32_t kClksel3Mask = 0x1Fu;

bool CoreEnDefined(uint32_t en) { return en >= 0x5u && en <= 0x7u; }

/* SPRUF98Y Table 4-177 (page 466): Reset Type W except CM_CLKOUT_CTRL (C); page 259: C = cold reset, W = warm
   reset; Table 4-180 (page 469) CM_CLKEN2_PLL 0x11, Table 4-186 (page 472), Table 4-188 (page 473), Table 4-196
   (page 476) 0, Table 4-198 (page 477) CM_CLKSEL5_PLL 0x01, Table 4-200 (page 478) CM_CLKOUT_CTRL 0x03. */
constexpr Omap3530RegReset kWarmReset[] = {{0x04u, 0x11u}, {0x30u, 0u}, {0x34u, 0u}, {0x4Cu, 0u}, {0x50u, 0x01u}};
constexpr Omap3530RegReset kColdReset[] = {{0x70u, 0x03u}};

}

void Omap3530CmClockControl::OnReady() {
    osc_hz_ = emu_.Get<Omap3530BoardClockSetup>().OscSysClkHz();
    Omap3530PrcmStubBlock::OnReady();
    {
        std::lock_guard<std::mutex> lk(mu_);
        LoadResetLocked(true);
    }
    auto& reset = emu_.Get<GuestCpuReset>();
    reset.RegisterResetListener([this](ResetLineKind kind) {
        std::lock_guard<std::mutex> lk(mu_);
        LoadResetLocked(kind == ResetLineKind::Rtc);
    });
    reset.RegisterResetReleaseListener([this] {
        if (emu_.Get<GuestCpuReset>().DeliveredResetWasResume()) {
            emu_.Get<Fatal>().Die("omap3530 CM_CLOCK_CONTROL: a resume reset; the DPLL4 "
                                  "state across sleep is not modelled");
        }
        NotifyRate();
    });
}

const char* Omap3530CmClockControl::RegisterName(uint32_t off) const {
    switch (off) {
    case 0x00: return "CM_CLKEN_PLL";
    case 0x04: return "CM_CLKEN2_PLL";
    case 0x20: return "CM_IDLEST_CKGEN";
    case 0x24: return "CM_IDLEST2_CKGEN";
    case 0x30: return "CM_AUTOIDLE_PLL";
    case 0x34: return "CM_AUTOIDLE2_PLL";
    case 0x40: return "CM_CLKSEL1_PLL";
    case 0x44: return "CM_CLKSEL2_PLL";
    case 0x48: return "CM_CLKSEL3_PLL";
    case 0x4C: return "CM_CLKSEL4_PLL";
    case 0x50: return "CM_CLKSEL5_PLL";
    case 0x70: return "CM_CLKOUT_CTRL";
    }
    return nullptr;
}

void Omap3530CmClockControl::LoadResetLocked(bool cold) {
    StoreResetValuesLocked(kWarmReset);
    if (cold) StoreResetValuesLocked(kColdReset);
    auto& setup = emu_.Get<Omap3530BoardClockSetup>();
    const Omap3530PeriphDpllSetting per  = setup.BootPeriphDpll();
    const Omap3530CoreClockSetting  core = setup.BootCoreClocks();
    regs_[kOffClkenPll / 4u] = (per.clken_pll & kDpll4Fields & ~kDpll4Reserved) |
                               (core.clken_pll_core & kCoreWritable);
    regs_[kOffClksel2Pll / 4u] = per.clksel2_pll & kClksel2Mask;
    regs_[kOffClksel1Pll / 4u] = core.clksel1_pll & kClksel1Mask;
    regs_[kOffClksel3Pll / 4u] = core.clksel3_pll & kClksel3Mask;
}

/* SPRUF98Y Figure 4-40 (printed p. 310): the DPLL4 reference is SYS_CLK and M4 feeds
   DSS1_ALWON_FCLK. */
bool Omap3530CmClockControl::DeriveDpll4Locked(GuestCycleClock::Rate& rate,
                                               const char*& why) const {
    const uint32_t clken = regs_[kOffClkenPll / 4u];
    if (clken & kLpMode) {
        why = "EN_PERIPH_DPLL_LPMODE selects the LP mode";
        return false;
    }
    why = Omap3530DpllClkoutX2(osc_hz_, (clken & kEnMask) >> kEnShift,
                               (clken & kFreqSelMask) >> kFreqSelShift,
                               regs_[kOffClksel2Pll / 4u], rate.num, rate.den);
    return why == nullptr;
}

GuestCycleClock::Rate Omap3530CmClockControl::Dpll4M4X2Input() const {
    std::lock_guard<std::mutex> lk(mu_);
    GuestCycleClock::Rate rate;
    const char* why = nullptr;
    if (!DeriveDpll4Locked(rate, why)) {
        emu_.Get<Fatal>().Die("omap3530 CM_CLOCK_CONTROL: DPLL4 %s (CLKEN 0x%08X CLKSEL2 "
                              "0x%08X); that DSS1_ALWON_FCLK source is not modelled", why,
                              regs_[kOffClkenPll / 4u], regs_[kOffClksel2Pll / 4u]);
    }
    if (regs_[kOffClkenPll / 4u] & kPwrdnDss1) {
        emu_.Get<Fatal>().Die("omap3530 CM_CLOCK_CONTROL: PWRDN_DSS1 powers down the DPLL4 "
                              "M4X2 path (CLKEN 0x%08X); a stopped DSS1_ALWON_FCLK is not "
                              "modelled", regs_[kOffClkenPll / 4u]);
    }
    LOG(Periph, "[CM_CLKCTRL] DPLL4 CLKOUTX2 %llu/%llu Hz (CLKEN 0x%08X CLKSEL2 0x%08X)\n",
        static_cast<unsigned long long>(rate.num), static_cast<unsigned long long>(rate.den),
        regs_[kOffClkenPll / 4u], regs_[kOffClksel2Pll / 4u]);
    return rate;
}

/* SPRUF98Y section 4.7.3.3 / Figure 4-38 (printed p. 307): DPLL3 takes one input as its reference
   and bypass clock, CLKOUT = CLKOUTX2 / 2, CLKOUT_M2 = CLKOUT / M2, and the bypass clock reaches the
   outputs past the dividers; Table 4-37 (printed p. 333): a bypass mode outputs the bypass frequency. */
GuestCycleClock::Rate Omap3530CmClockControl::CoreClkLocked() const {
    const uint32_t clken   = regs_[kOffClkenPll / 4u];
    const uint32_t clksel1 = regs_[kOffClksel1Pll / 4u];
    const uint32_t mult    = (clksel1 >> 16) & 0x7FFu;
    if ((clken & kCoreEnMask) != kEnLock || mult <= 1u) return GuestCycleClock::Rate{osc_hz_, 1u};
    GuestCycleClock::Rate rate;
    const char* why = Omap3530DpllClkoutX2(osc_hz_, kEnLock, (clken >> kCoreFreqSelShift) & 0xFu,
                                           (mult << 8) | ((clksel1 >> 8) & 0x7Fu), rate.num, rate.den);
    if (why != nullptr) {
        emu_.Get<Fatal>().Die("omap3530 CM_CLOCK_CONTROL: DPLL3 %s (CLKEN 0x%08X CLKSEL1 0x%08X)", why,
                              clken, clksel1);
    }
    rate.den *= 2u * (clksel1 >> 27);
    return rate;
}

/* SPRUF98Y Figure 4-37 (printed p. 306): DPLL4_M2X2_CLK through the PRM to CM_96M_FCLK, and a mux of
   CM_96M_FCLK and CM_SYS_CLK to 96M_FCLK; Table 4-41 (printed p. 335): PWRDN_96M powers down the
   DPLL4 M2X2 output. */
GuestCycleClock::Rate Omap3530CmClockControl::Fclk96MLocked() const {
    if ((regs_[kOffClksel1Pll / 4u] & kSource96M) != 0u) return GuestCycleClock::Rate{osc_hz_, 1u};
    if ((regs_[kOffClkenPll / 4u] & kPwrdn96M) != 0u) return GuestCycleClock::Rate{0u, 1u};
    GuestCycleClock::Rate rate;
    const char* why = nullptr;
    if (!DeriveDpll4Locked(rate, why)) {
        emu_.Get<Fatal>().Die("omap3530 CM_CLOCK_CONTROL: DPLL4 %s (CLKEN 0x%08X CLKSEL2 0x%08X); that "
                              "96M_FCLK source is not modelled", why, regs_[kOffClkenPll / 4u],
                              regs_[kOffClksel2Pll / 4u]);
    }
    rate.den *= regs_[kOffClksel3Pll / 4u] & kClksel3Mask;
    return rate;
}

GuestCycleClock::Rate Omap3530CmClockControl::CoreClk() const {
    std::lock_guard<std::mutex> lk(mu_);
    return CoreClkLocked();
}

GuestCycleClock::Rate Omap3530CmClockControl::Fclk96M() const {
    std::lock_guard<std::mutex> lk(mu_);
    return Fclk96MLocked();
}

void Omap3530CmClockControl::RegisterRateListener(std::function<void()> fn) {
    rate_listeners_.push_back(std::move(fn));
}

void Omap3530CmClockControl::NotifyRate() {
    for (auto& fn : rate_listeners_) fn();
}

/* Table 4-182 (printed p. 469-470): CM_IDLEST_CKGEN reports ST_PERIPH_CLK [1] DPLL4
   locked and ST_DSS1_CLK [11] DSS1_ALWON_FCLK active next to ST_CORE_CLK [0] DPLL3
   and the activity of the other DPLL3/DPLL4 output clocks. */
uint32_t Omap3530CmClockControl::ReadWord(uint32_t addr) {
    if (addr - MmioBase() == kOffIdlestCkgen) {
        emu_.Get<Fatal>().Die("omap3530 CM_CLOCK_CONTROL: CM_IDLEST_CKGEN read; the clock "
                              "activity status it reports is not modelled");
    }
    return Omap3530PrcmStubBlock::ReadWord(addr);
}

uint16_t Omap3530CmClockControl::ReadHalf(uint32_t addr) {
    if (((addr - MmioBase()) & ~3u) == kOffIdlestCkgen) {
        HaltUnsupportedAccess("ReadHalf(CM_IDLEST_CKGEN)", addr, 0);
    }
    return Omap3530PrcmStubBlock::ReadHalf(addr);
}

uint32_t Omap3530CmClockControl::CheckedWriteLocked(uint32_t off, uint32_t value) const {
    auto& fatal = emu_.Get<Fatal>();
    const uint32_t prev = regs_[off / 4u];
    if (off == kOffClkenPll) {
        const uint32_t next = (value & kDpll4Fields & ~kDpll4Reserved) | (value & kCoreWritable);
        if (((prev ^ next) & kDpll4RateBits) != 0u) {
            fatal.Die("omap3530 CM_CLOCK_CONTROL: write 0x%08X to +0x%02X (was 0x%08X) changes the "
                      "DPLL4 mode, FREQSEL, LP mode or M4X2 power; DPLL4 relock, bypass and "
                      "power-down are not modelled", value, off, prev);
        }
        if (!CoreEnDefined(next & kCoreEnMask)) {
            fatal.Die("omap3530 CM_CLOCK_CONTROL: CM_CLKEN_PLL write 0x%08X selects the reserved "
                      "EN_CORE_DPLL %u", value, next & kCoreEnMask);
        }
        return next;
    }
    if (off == kOffClksel2Pll) {
        const uint32_t next = value & kClksel2Mask;
        if (((prev ^ next) & kClksel2Mask) != 0u) {
            fatal.Die("omap3530 CM_CLOCK_CONTROL: write 0x%08X to +0x%02X (was 0x%08X) changes the "
                      "DPLL4 M/N; DPLL4 relock is not modelled", value, off, prev);
        }
        return next;
    }
    if (off == kOffClksel1Pll) {
        const uint32_t next = value & kClksel1Mask;
        if ((next >> 27) == 0u) {
            fatal.Die("omap3530 CM_CLOCK_CONTROL: CM_CLKSEL1_PLL write 0x%08X selects the reserved "
                      "CORE_DPLL_CLKOUT_DIV 0", value);
        }
        return next;
    }
    const uint32_t next = value & kClksel3Mask;
    if (next == 0u || next > 16u) {
        fatal.Die("omap3530 CM_CLOCK_CONTROL: CM_CLKSEL3_PLL write 0x%08X selects the reserved DIV_96M "
                  "%u", value, next);
    }
    return next;
}

void Omap3530CmClockControl::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - MmioBase();
    if (off != kOffClkenPll && off != kOffClksel1Pll && off != kOffClksel2Pll &&
        off != kOffClksel3Pll) {
        Omap3530PrcmStubBlock::WriteWord(addr, value);
        return;
    }
    bool changed = false;
    {
        std::lock_guard<std::mutex> lk(mu_);
        const GuestCycleClock::Rate core = CoreClkLocked();
        const GuestCycleClock::Rate fclk = Fclk96MLocked();
        regs_[off / 4u] = CheckedWriteLocked(off, value);
        changed = !(CoreClkLocked().Reduced() == core.Reduced()) ||
                  !(Fclk96MLocked().Reduced() == fclk.Reduced());
    }
    if (changed) NotifyRate();
}

void Omap3530CmClockControl::WriteHalf(uint32_t addr, uint16_t value) {
    const uint32_t off = (addr - MmioBase()) & ~3u;
    if (off == kOffClkenPll || off == kOffClksel2Pll) {
        HaltUnsupportedAccess("WriteHalf(DPLL4 control register)", addr, value);
    }
    if (off == kOffClksel1Pll || off == kOffClksel3Pll) {
        WriteWord(MmioBase() + off, WordWithHalf(addr, value));
        return;
    }
    Omap3530PrcmStubBlock::WriteHalf(addr, value);
}

REGISTER_SERVICE(Omap3530CmClockControl);

}
