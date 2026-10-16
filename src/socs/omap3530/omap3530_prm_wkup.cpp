#include "omap3530_prcm_stub_block.h"

namespace {

class Omap3530PrmWkup : public Omap3530PrcmStubBlock {
public:
    using Omap3530PrcmStubBlock::Omap3530PrcmStubBlock;

    uint32_t MmioBase() const override { return 0x48306C00u; }
    uint32_t MmioSize() const override { return 0x00000100u; }

protected:
    const char* Label() const override { return "PRM_WKUP"; }

    const char* RegisterName(uint32_t off) const override {
        switch (off) {
        case 0xA0: return "PM_WKEN_WKUP";
        case 0xA4: return "PM_MPUGRPSEL_WKUP";
        case 0xA8: return "PM_IVA2GRPSEL_WKUP";
        case 0xB0: return "PM_WKST_WKUP";
        }
        return nullptr;
    }
};

}  /* namespace */

REGISTER_SERVICE(Omap3530PrmWkup);
