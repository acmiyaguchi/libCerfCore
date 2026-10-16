#include "omap3530_prcm_stub_block.h"

#include <cerfcore/core/fatal.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "omap3530_board_clock_setup.h"

namespace {

constexpr uint32_t kOffClksrcCtrl = 0x70u;

/* SPRUF98Y Table 4-456 (printed p. 596): PRM_CLKSRC_CTRL SYSCLKDIV [7:6] reset
   0x1, "0x1: Syst_clk is external clock / 1"; AUTOEXTCLKMODE [4:3] RW;
   SYSCLKSEL [1:0] R, 0x0 bypass (external square clock), 0x1 oscillator. */
constexpr uint32_t kSysClkDivMask      = 3u << 6;
constexpr uint32_t kSysClkDivBy1       = 1u << 6;
constexpr uint32_t kAutoExtClkModeMask = 3u << 3;
constexpr uint32_t kSysClkSelMask      = 3u;
constexpr uint32_t kSysClkSelBypass    = 0u;
constexpr uint32_t kSysClkSelOsc       = 1u;

class Omap3530PrmGlobal : public Omap3530PrcmStubBlock {
public:
    using Omap3530PrcmStubBlock::Omap3530PrcmStubBlock;

    uint32_t MmioBase() const override { return 0x48307200u; }
    uint32_t MmioSize() const override { return 0x00000100u; }

    void OnReady() override {
        Omap3530PrcmStubBlock::OnReady();
        sysclksel_ = emu_.Get<Omap3530BoardClockSetup>().SysXtalinIsSquareClock()
                         ? kSysClkSelBypass
                         : kSysClkSelOsc;
        {
            std::lock_guard<std::mutex> lk(mu_);
            regs_[kOffClksrcCtrl / 4u] = kSysClkDivBy1 | sysclksel_;
        }
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind kind) {
            if (kind != ResetLineKind::Rtc) return;
            std::lock_guard<std::mutex> lk(mu_);
            regs_[kOffClksrcCtrl / 4u] = kSysClkDivBy1 | sysclksel_;
        });
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        if (addr - MmioBase() != kOffClksrcCtrl) {
            Omap3530PrcmStubBlock::WriteWord(addr, value);
            return;
        }
        if ((value & kSysClkDivMask) != kSysClkDivBy1) {
            emu_.Get<Fatal>().Die("omap3530 PRM_CLKSRC_CTRL write 0x%08X sets SYSCLKDIV %u; "
                                  "only the divide-by-1 SYS_CLK is modelled",
                                  value, (value & kSysClkDivMask) >> 6);
        }
        std::lock_guard<std::mutex> lk(mu_);
        uint32_t& reg = regs_[kOffClksrcCtrl / 4u];
        reg = (reg & ~kAutoExtClkModeMask) | (value & kAutoExtClkModeMask);
    }

    void WriteHalf(uint32_t addr, uint16_t value) override {
        if ((addr - MmioBase()) / 4u == kOffClksrcCtrl / 4u) {
            HaltUnsupportedAccess("WriteHalf(PRM_CLKSRC_CTRL)", addr, value);
        }
        Omap3530PrcmStubBlock::WriteHalf(addr, value);
    }

    void RestoreState(StateReader& r) override {
        std::lock_guard<std::mutex> lk(mu_);
        RestoreRegsLocked(r);
        if ((regs_[kOffClksrcCtrl / 4u] & kSysClkDivMask) != kSysClkDivBy1) {
            r.Reject("omap3530 PRM_CLKSRC_CTRL: restored SYSCLKDIV is not divide-by-1");
        }
        if ((regs_[kOffClksrcCtrl / 4u] & kSysClkSelMask) != sysclksel_) {
            r.Reject("omap3530 PRM_CLKSRC_CTRL: restored SYSCLKSEL %u is not the "
                     "board's %u", regs_[kOffClksrcCtrl / 4u] & kSysClkSelMask,
                     sysclksel_);
        }
    }

protected:
    const char* Label() const override { return "PRM_GLOBAL"; }

    const char* RegisterName(uint32_t off) const override {
        switch (off) {
        case 0x20: return "PRM_VC_SMPS_SA";
        case 0x24: return "PRM_VC_SMPS_VOL_RA";
        case 0x28: return "PRM_VC_SMPS_CMD_RA";
        case 0x2C: return "PRM_VC_CMD_VAL_0";
        case 0x30: return "PRM_VC_CMD_VAL_1";
        case 0x34: return "PRM_VC_CH_CONF";
        case 0x38: return "PRM_VC_I2C_CFG";
        case 0x3C: return "PRM_VC_BYPASS_VAL";
        case 0x50: return "PRM_RSTCTRL";
        case 0x54: return "PRM_RSTTIME";
        case 0x58: return "PRM_RSTST";
        case 0x60: return "PRM_VOLTCTRL";
        case 0x64: return "PRM_SRAM_PCHARGE";
        case 0x70: return "PRM_CLKSRC_CTRL";
        case 0x80: return "PRM_OBS";
        case 0x90: return "PRM_VOLTSETUP1";
        case 0x94: return "PRM_VOLTOFFSET";
        case 0x98: return "PRM_CLKSETUP";
        case 0x9C: return "PRM_POLCTRL";
        case 0xA0: return "PRM_VOLTSETUP2";
        case 0xB0: return "PRM_VP1_CONFIG";
        case 0xB4: return "PRM_VP1_VSTEPMIN";
        case 0xB8: return "PRM_VP1_VSTEPMAX";
        case 0xBC: return "PRM_VP1_VLIMITTO";
        case 0xC0: return "PRM_VP1_VOLTAGE";
        case 0xC4: return "PRM_VP1_STATUS";
        case 0xD0: return "PRM_VP2_CONFIG";
        case 0xD4: return "PRM_VP2_VSTEPMIN";
        case 0xD8: return "PRM_VP2_VSTEPMAX";
        case 0xDC: return "PRM_VP2_VLIMITTO";
        case 0xE0: return "PRM_VP2_VOLTAGE";
        case 0xE4: return "PRM_VP2_STATUS";
        }
        return nullptr;
    }

private:
    uint32_t sysclksel_ = 0;
};

}

REGISTER_SERVICE(Omap3530PrmGlobal);
