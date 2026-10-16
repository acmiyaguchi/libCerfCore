#include "omap3530_prcm_stub_block.h"

namespace {

class Omap3530PrmPer : public Omap3530PrmDomainBlock {
public:
    using Omap3530PrmDomainBlock::Omap3530PrmDomainBlock;

    uint32_t MmioBase() const override { return 0x48307000u; }
    uint32_t MmioSize() const override { return 0x00000100u; }

protected:
    const char* Label() const override { return "PRM_PER"; }

    const char* RegisterName(uint32_t off) const override {
        switch (off) {
        case 0x58: return "RM_RSTST_PER";
        case 0xA0: return "PM_WKEN_PER";
        case 0xA4: return "PM_MPUGRPSEL_PER";
        case 0xA8: return "PM_IVA2GRPSEL_PER";
        case 0xB0: return "PM_WKST_PER";
        case 0xC8: return "PM_WKDEP_PER";
        case 0xE0: return "PM_PWSTCTRL_PER";
        case 0xE4: return "PM_PWSTST_PER";
        case 0xE8: return "PM_PREPWSTST_PER";
        }
        return nullptr;
    }
};

}  /* namespace */

REGISTER_SERVICE(Omap3530PrmPer);
