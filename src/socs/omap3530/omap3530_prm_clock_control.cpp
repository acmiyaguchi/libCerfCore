#include "omap3530_prcm_stub_block.h"

#include <cerfcore/core/fatal.h>
#include "omap3530_board_clock_setup.h"

namespace {

constexpr uint32_t kOffClksel = 0x40u;

/* SPRUF98Y Table 4-377 (printed p. 560): PRM_CLKSEL SYS_CLKIN_SEL [2:0] RW,
   0x0 12 MHz, 0x1 13 MHz, 0x2 19.2 MHz, 0x3 26 MHz, 0x4 38.4 MHz, 0x5 16.8 MHz;
   bits [31:3] read 0; "reset on power-up only". */
constexpr uint32_t kSysClkInSelMask = 0x7u;
constexpr uint64_t kSysClkInHz[]    = {
    12000000u, 13000000u, 19200000u, 26000000u, 38400000u, 16800000u,
};

class Omap3530PrmClockControl : public Omap3530PrcmStubBlock {
public:
    using Omap3530PrcmStubBlock::Omap3530PrcmStubBlock;

    uint32_t MmioBase() const override { return 0x48306D00u; }
    uint32_t MmioSize() const override { return 0x00000100u; }

    void OnReady() override {
        Omap3530PrcmStubBlock::OnReady();
        const uint64_t osc = emu_.Get<Omap3530BoardClockSetup>().OscSysClkHz();
        uint32_t sel = kSysClkInSelMask + 1u;
        for (uint32_t i = 0; i < sizeof(kSysClkInHz) / sizeof(kSysClkInHz[0]); ++i) {
            if (kSysClkInHz[i] == osc) sel = i;
        }
        if (sel > kSysClkInSelMask) {
            emu_.Get<Fatal>().Die("omap3530 PRM_CLKSEL: the board OSC_SYS_CLK %llu Hz has "
                                  "no SYS_CLKIN_SEL encoding",
                                  static_cast<unsigned long long>(osc));
        }
        std::lock_guard<std::mutex> lk(mu_);
        regs_[kOffClksel / 4u] = sel;
        boot_sel_              = sel;
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        if (addr - MmioBase() != kOffClksel) {
            Omap3530PrcmStubBlock::WriteWord(addr, value);
            return;
        }
        if ((value & kSysClkInSelMask) != boot_sel_) {
            emu_.Get<Fatal>().Die("omap3530 PRM_CLKSEL write 0x%08X declares SYS_CLKIN_SEL "
                                  "%u against the fitted oscillator's %u; that input is not "
                                  "modelled", value, value & kSysClkInSelMask, boot_sel_);
        }
    }

    void WriteHalf(uint32_t addr, uint16_t value) override {
        if ((addr - MmioBase()) / 4u == kOffClksel / 4u) {
            HaltUnsupportedAccess("WriteHalf(PRM_CLKSEL)", addr, value);
        }
        Omap3530PrcmStubBlock::WriteHalf(addr, value);
    }

    void RestoreState(StateReader& r) override {
        std::lock_guard<std::mutex> lk(mu_);
        RestoreRegsLocked(r);
        if (regs_[kOffClksel / 4u] != boot_sel_) {
            r.Reject("omap3530 PRM_CLKSEL: restored 0x%08X does not declare the fitted "
                     "oscillator's SYS_CLKIN_SEL %u", regs_[kOffClksel / 4u], boot_sel_);
        }
    }

protected:
    const char* Label() const override { return "PRM_CLOCK_CONTROL"; }

    const char* RegisterName(uint32_t off) const override {
        switch (off) {
        case 0x40: return "PRM_CLKSEL";
        case 0x70: return "PRM_CLKOUT_CTRL";
        }
        return nullptr;
    }

private:
    uint32_t boot_sel_ = 0;
};

}

REGISTER_SERVICE(Omap3530PrmClockControl);
