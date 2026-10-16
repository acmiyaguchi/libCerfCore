#include "omap3530_prcm_stub_block.h"

namespace {

class Omap3530PrmIva2 : public Omap3530PrmDomainBlock {
public:
    using Omap3530PrmDomainBlock::Omap3530PrmDomainBlock;

    uint32_t MmioBase() const override { return 0x48306000u; }
    uint32_t MmioSize() const override { return 0x00000100u; }

protected:
    const char* Label() const override { return "PRM_IVA2"; }

    const char* RegisterName(uint32_t off) const override {
        switch (off) {
        case 0x50: return "RM_RSTCTRL_IVA2";
        case 0x58: return "RM_RSTST_IVA2";
        case 0xC8: return "PM_WKDEP_IVA2";
        case 0xE0: return "PM_PWSTCTRL_IVA2";
        case 0xE4: return "PM_PWSTST_IVA2";
        case 0xE8: return "PM_PREPWSTST_IVA2";
        case 0xF8: return "PRM_IRQSTATUS_IVA2";
        case 0xFC: return "PRM_IRQENABLE_IVA2";
        }
        return nullptr;
    }
};

}  /* namespace */

REGISTER_SERVICE(Omap3530PrmIva2);
