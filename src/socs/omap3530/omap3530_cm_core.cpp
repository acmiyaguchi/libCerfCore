#include "omap3530_cm_core.h"

#include <cerfcore/core/fatal.h>
#include "omap3530_board_clock_setup.h"
#include "omap3530_cm_clock_control.h"

namespace {

constexpr uint32_t kOffClkselCore = 0x40u;
constexpr uint32_t kOffClkstctrl  = 0x48u;
constexpr uint32_t kOffClkstst    = 0x4Cu;

/* SPRUF98Y Table 4-145 (printed p. 455): CM_CLKSEL_CORE [11:8] "Write the reset value, read returns
   reset value" (0x1), CLKSEL_GPT11 [7], CLKSEL_GPT10 [6], CLKSEL_L4 [3:2] and CLKSEL_L3 [1:0] divide
   by 1 or 2, "Other enums: Reserved". */
constexpr uint32_t kClkselStored   = 0xCFu;
constexpr uint32_t kClkselReserved = 0x100u;

/* SPRUF98Y Table 4-129 CM_FCLKEN1_CORE (page 443) and Table 4-133 CM_ICLKEN1_CORE (page 446):
   EN_MCBSP1 [9], EN_MCBSP5 [10]. */
constexpr uint32_t kMcBspBits  = (1u << 9) | (1u << 10);

/* SPRUF98Y Table 4-128 (page 443): the Reset Type W registers; Table 4-129 (page 445), Table 4-131
   (page 445), Table 4-135 (page 448), Table 4-143 (page 454), Table 4-147 (page 456): reset 0; Table 4-133 (page 447):
   EN_OMAPCTRL [6] and EN_SDRC [1] reset 1, bit 3 reads 0; Table 4-141 (page 454): AUTOIDLE1 bit 3 reset 1. */
constexpr Omap3530RegReset kWarmReset[] = {
    {0x00u, 0u},     {0x08u, 0u}, {0x10u, (1u << 6) | (1u << 1)}, {0x14u, 0u}, {0x18u, 0u},
    {0x30u, 1u << 3}, {0x34u, 0u}, {0x38u, 0u},                  {0x48u, 0u},
};

uint32_t L3Div(uint32_t clksel) { return clksel & 3u; }
uint32_t L4Div(uint32_t clksel) { return (clksel >> 2) & 3u; }
bool     DividerDefined(uint32_t d) { return d == 1u || d == 2u; }

}

void Omap3530CmCore::OnReady() {
    clock_control_ = &emu_.Get<Omap3530CmClockControl>();
    Omap3530CmMcBspGateBlock::OnReady();
    clock_control_->RegisterRateListener([this] { NotifyClock(); });
}

const char* Omap3530CmCore::RegisterName(uint32_t off) const {
    switch (off) {
    case 0x00: return "CM_FCLKEN1_CORE";
    case 0x08: return "CM_FCLKEN3_CORE";
    case 0x10: return "CM_ICLKEN1_CORE";
    case 0x14: return "CM_ICLKEN2_CORE";
    case 0x18: return "CM_ICLKEN3_CORE";
    case 0x20: return "CM_IDLEST1_CORE";
    case 0x24: return "CM_IDLEST2_CORE";
    case 0x28: return "CM_IDLEST3_CORE";
    case 0x30: return "CM_AUTOIDLE1_CORE";
    case 0x34: return "CM_AUTOIDLE2_CORE";
    case 0x38: return "CM_AUTOIDLE3_CORE";
    case 0x40: return "CM_CLKSEL_CORE";
    case 0x48: return "CM_CLKSTCTRL_CORE";
    case 0x4C: return "CM_CLKSTST_CORE";
    }
    return nullptr;
}

uint32_t Omap3530CmCore::McBspBit(uint32_t port) const { return port == 1u ? 1u << 9 : 1u << 10; }

uint32_t Omap3530CmCore::McBspMask() const { return kMcBspBits; }

bool Omap3530CmCore::LoadResetLocked() {
    const uint32_t before = regs_[kOffClkselCore / 4u];
    const uint32_t boot   = emu_.Get<Omap3530BoardClockSetup>().BootCoreClocks().clksel_core;
    StoreResetValuesLocked(kWarmReset);
    const uint32_t next        = (boot & kClkselStored) | kClkselReserved;
    regs_[kOffClkselCore / 4u] = next;
    return L3Div(before) != L3Div(next) || L4Div(before) != L4Div(next);
}

uint32_t Omap3530CmCore::ReadWord(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    if (off == kOffClkstst) {
        const uint32_t ctrl = PeekReg(kOffClkstctrl);
        uint32_t st = 0;
        for (uint32_t i = 0; i < 16u; ++i) {
            if (((ctrl >> (i * 2u)) & 0x3u) != 1u) {
                st |= 1u << i;
            }
        }
        return st;
    }
    return Omap3530PrcmStubBlock::ReadWord(addr);
}

void Omap3530CmCore::WriteWord(uint32_t addr, uint32_t value) {
    if (addr - MmioBase() != kOffClkselCore) {
        Omap3530CmMcBspGateBlock::WriteWord(addr, value);
        return;
    }
    if (!DividerDefined(L3Div(value)) || !DividerDefined(L4Div(value))) {
        emu_.Get<Fatal>().Die("omap3530 CM_CORE: CM_CLKSEL_CORE write 0x%08X selects a reserved "
                              "CLKSEL_L3 or CLKSEL_L4", value);
    }
    bool changed = false;
    {
        std::lock_guard<std::mutex> lk(mu_);
        const uint32_t prev = regs_[kOffClkselCore / 4u];
        const uint32_t next = (value & kClkselStored) | kClkselReserved;
        regs_[kOffClkselCore / 4u] = next;
        changed = L3Div(prev) != L3Div(next) || L4Div(prev) != L4Div(next);
    }
    if (changed) NotifyClock();
}

void Omap3530CmCore::WriteHalf(uint32_t addr, uint16_t value) {
    const uint32_t off = (addr - MmioBase()) & ~3u;
    if (off == kOffClkselCore) {
        WriteWord(MmioBase() + off, WordWithHalf(addr, value));
        return;
    }
    Omap3530CmMcBspGateBlock::WriteHalf(addr, value);
}

/* SPRUF98Y Figure 4-37 (printed p. 306): CORE_CLK through a [1, 2] divider to L3_ICLK and a second
   [1, 2] divider to L4_ICLK. */
GuestCycleClock::Rate Omap3530CmCore::L4Iclk() const {
    GuestCycleClock::Rate rate = clock_control_->CoreClk();
    const uint32_t clksel = PeekReg(kOffClkselCore);
    rate.den *= L3Div(clksel) * L4Div(clksel);
    return rate;
}

REGISTER_SERVICE(Omap3530CmCore);
