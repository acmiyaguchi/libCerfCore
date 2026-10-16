#include "omap3530_prcm_stub_block.h"

namespace {

class Omap3530PrmNeon : public Omap3530PrmDomainBlock {
public:
    using Omap3530PrmDomainBlock::Omap3530PrmDomainBlock;

    uint32_t MmioBase() const override { return 0x48307300u; }
    uint32_t MmioSize() const override { return 0x00000100u; }

protected:
    const char* Label() const override { return "PRM_NEON"; }

    const char* RegisterName(uint32_t off) const override {
        switch (off) {
        case 0x58: return "RM_RSTST_NEON";
        case 0xC8: return "PM_WKDEP_NEON";
        case 0xE0: return "PM_PWSTCTRL_NEON";
        case 0xE4: return "PM_PWSTST_NEON";
        case 0xE8: return "PM_PREPWSTST_NEON";
        }
        return nullptr;
    }
};

}  /* namespace */

REGISTER_SERVICE(Omap3530PrmNeon);
