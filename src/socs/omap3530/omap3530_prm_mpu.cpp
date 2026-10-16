#include "omap3530_prcm_stub_block.h"

namespace {

class Omap3530PrmMpu : public Omap3530PrmDomainBlock {
public:
    using Omap3530PrmDomainBlock::Omap3530PrmDomainBlock;

    uint32_t MmioBase() const override { return 0x48306900u; }
    uint32_t MmioSize() const override { return 0x00000100u; }

protected:
    const char* Label() const override { return "PRM_MPU"; }

    const char* RegisterName(uint32_t off) const override {
        switch (off) {
        case 0x58: return "RM_RSTST_MPU";
        case 0xC8: return "PM_WKDEP_MPU";
        case 0xD4: return "PM_EVGENCTRL_MPU";
        case 0xD8: return "PM_EVGENONTIM_MPU";
        case 0xDC: return "PM_EVGENOFFTIM_MPU";
        case 0xE0: return "PM_PWSTCTRL_MPU";
        case 0xE4: return "PM_PWSTST_MPU";
        case 0xE8: return "PM_PREPWSTST_MPU";
        }
        return nullptr;
    }
};

}  /* namespace */

REGISTER_SERVICE(Omap3530PrmMpu);
