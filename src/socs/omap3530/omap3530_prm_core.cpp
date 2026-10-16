#include "omap3530_prcm_stub_block.h"

namespace {

class Omap3530PrmCore : public Omap3530PrmDomainBlock {
public:
    using Omap3530PrmDomainBlock::Omap3530PrmDomainBlock;

    uint32_t MmioBase() const override { return 0x48306A00u; }
    uint32_t MmioSize() const override { return 0x00000100u; }

protected:
    const char* Label() const override { return "PRM_CORE"; }

    const char* RegisterName(uint32_t off) const override {
        switch (off) {
        case 0x50: return "RM_RSTCTRL_CORE";
        case 0x58: return "RM_RSTST_CORE";
        case 0xA0: return "PM_WKEN1_CORE";
        case 0xA4: return "PM_MPUGRPSEL1_CORE";
        case 0xA8: return "PM_IVA2GRPSEL1_CORE";
        case 0xB0: return "PM_WKST1_CORE";
        case 0xB8: return "PM_WKST3_CORE";
        case 0xE0: return "PM_PWSTCTRL_CORE";
        case 0xE4: return "PM_PWSTST_CORE";
        case 0xE8: return "PM_PREPWSTST_CORE";
        case 0xF0: return "PM_WKEN3_CORE";
        case 0xF4: return "PM_IVA2GRPSEL3_CORE";
        case 0xF8: return "PM_MPUGRPSEL3_CORE";
        }
        return nullptr;
    }
};

}  /* namespace */

REGISTER_SERVICE(Omap3530PrmCore);
