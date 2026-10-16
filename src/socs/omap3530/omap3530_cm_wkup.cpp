#include "omap3530_prcm_stub_block.h"

#include <cerfcore/core/fatal.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "omap3530_board_clock_setup.h"

namespace {

constexpr uint32_t kOffFclkenWkup = 0x00u;
constexpr uint32_t kOffIclkenWkup = 0x10u;
constexpr uint32_t kOffClkselWkup = 0x40u;

/* SPRUF98Y Table 4-167 (printed p. 461-462) CM_FCLKEN_WKUP and Table 4-169
   (printed p. 462-463) CM_ICLKEN_WKUP: every field resets to 0x0; EN_GPT1 [0]
   RW, "0x1: GPTIMER 1 functional clock is enabled" / "interface clock". */
constexpr uint32_t kEnGpt1 = 1u << 0;

/* Table 4-167: FCLKEN_WKUP [9], [7:6], [5], [3], [0] RW, all other bits R "Read
   returns 0". Table 4-169: ICLKEN_WKUP [9], [5:0] RW, all other bits R. */
constexpr uint32_t kFclkenWkupMask = 0x2E9u;
constexpr uint32_t kIclkenWkupMask = 0x23Fu;

/* OMAP3530 TRM SPRUF98Y Table 4-175 (printed p. 465): CM_CLKSEL_WKUP
   RESERVED [6:3] reset 0x2, CLKSEL_RM [2:1] reset 0x1, CLKSEL_GPT1 [0] reset
   0x0 ("0x0: source is 32K_FCLK", "0x1: source is SYS_CLK"). */
constexpr uint32_t kClkselWkupReset = 0x12u;
constexpr uint32_t kClkselGpt1      = 1u << 0;

/* Table 4-175: bits [31:7] are type R, "Read returns 0"; CLKSEL_RM defines
   only 0x1 (L4_CLK divided by 1) and 0x2 (divided by 2), "Other enums:
   Reserved". */
constexpr uint32_t kClkselWkupMask  = 0x7Fu;
constexpr uint32_t kClkselRmShift   = 1u;
constexpr uint32_t kClkselRmMask    = 3u << kClkselRmShift;

class Omap3530CmWkup : public Omap3530PrcmStubBlock {
public:
    using Omap3530PrcmStubBlock::Omap3530PrcmStubBlock;

    uint32_t MmioBase() const override { return 0x48004C00u; }
    uint32_t MmioSize() const override { return 0x00000100u; }

    void OnReady() override {
        Omap3530PrcmStubBlock::OnReady();
        boot_gpt1_ = emu_.Get<Omap3530BoardClockSetup>().BootEnablesGpt1Clocks() ? kEnGpt1 : 0u;
        {
            std::lock_guard<std::mutex> lk(mu_);
            SeedBootLocked();
        }
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
            std::lock_guard<std::mutex> lk(mu_);
            SeedBootLocked();
        });
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - MmioBase();
        if (off == kOffFclkenWkup || off == kOffIclkenWkup) {
            if ((PeekReg(off) & kEnGpt1) != 0u && (value & kEnGpt1) == 0u) {
                emu_.Get<Fatal>().Die("omap3530 %s write 0x%08X disables the GPTIMER1 %s "
                                      "clock; GPTIMER1 without that clock is not modelled",
                                      RegisterName(off), value,
                                      off == kOffFclkenWkup ? "functional" : "interface");
            }
            std::lock_guard<std::mutex> lk(mu_);
            regs_[off / 4u] = value & (off == kOffFclkenWkup ? kFclkenWkupMask : kIclkenWkupMask);
            return;
        }
        if (off != kOffClkselWkup) {
            Omap3530PrcmStubBlock::WriteWord(addr, value);
            return;
        }
        if ((value & kClkselGpt1) != 0u) {
            emu_.Get<Fatal>().Die("omap3530 CM_CLKSEL_WKUP write 0x%08X selects "
                                  "SYS_CLK for GPTIMER1; that source is not "
                                  "modelled", value);
        }
        const uint32_t rm = (value & kClkselRmMask) >> kClkselRmShift;
        if (rm != 1u && rm != 2u) {
            emu_.Get<Fatal>().Die("omap3530 CM_CLKSEL_WKUP write 0x%08X selects the "
                                  "reserved CLKSEL_RM %u", value, rm);
        }
        std::lock_guard<std::mutex> lk(mu_);
        regs_[kOffClkselWkup / 4u] = value & kClkselWkupMask;
    }

    void WriteHalf(uint32_t addr, uint16_t value) override {
        const uint32_t word = ((addr - MmioBase()) / 4u) * 4u;
        if (word == kOffFclkenWkup || word == kOffIclkenWkup || word == kOffClkselWkup) {
            HaltUnsupportedAccess("WriteHalf(CM_WKUP clock register)", addr, value);
        }
        Omap3530PrcmStubBlock::WriteHalf(addr, value);
    }

    void RestoreState(StateReader& r) override {
        std::lock_guard<std::mutex> lk(mu_);
        RestoreRegsLocked(r);
        const uint32_t clksel = regs_[kOffClkselWkup / 4u];
        const uint32_t rm     = (clksel & kClkselRmMask) >> kClkselRmShift;
        if ((clksel & ~kClkselWkupMask) != 0u || (clksel & kClkselGpt1) != 0u ||
            (rm != 1u && rm != 2u)) {
            r.Reject("omap3530 CM_CLKSEL_WKUP: restored 0x%08X sets a field this build "
                     "does not model", clksel);
        }
        const uint32_t fclken = regs_[kOffFclkenWkup / 4u];
        const uint32_t iclken = regs_[kOffIclkenWkup / 4u];
        if ((fclken & ~kFclkenWkupMask) != 0u || (iclken & ~kIclkenWkupMask) != 0u) {
            r.Reject("omap3530 CM_WKUP: restored FCLKEN 0x%08X / ICLKEN 0x%08X set "
                     "read-only bits", fclken, iclken);
        }
        if ((fclken & boot_gpt1_) != boot_gpt1_ || (iclken & boot_gpt1_) != boot_gpt1_) {
            r.Reject("omap3530 CM_WKUP: restored image has the GPTIMER1 clocks disabled");
        }
    }

protected:
    const char* Label() const override { return "CM_WKUP"; }

    const char* RegisterName(uint32_t off) const override {
        switch (off) {
        case 0x00: return "CM_FCLKEN_WKUP";
        case 0x10: return "CM_ICLKEN_WKUP";
        case 0x20: return "CM_IDLEST_WKUP";
        case 0x30: return "CM_AUTOIDLE_WKUP";
        case 0x40: return "CM_CLKSEL_WKUP";
        }
        return nullptr;
    }

private:
    void SeedBootLocked() {
        regs_[kOffFclkenWkup / 4u] = boot_gpt1_;
        regs_[kOffIclkenWkup / 4u] = boot_gpt1_;
        regs_[kOffClkselWkup / 4u] = kClkselWkupReset;
    }

    uint32_t boot_gpt1_ = 0;
};

}

REGISTER_SERVICE(Omap3530CmWkup);
