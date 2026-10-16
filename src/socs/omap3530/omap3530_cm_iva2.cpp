#include "omap3530_prcm_stub_block.h"

namespace {

class Omap3530CmIva2 : public Omap3530PrcmStubBlock {
public:
    using Omap3530PrcmStubBlock::Omap3530PrcmStubBlock;

    uint32_t MmioBase() const override { return 0x48004000u; }
    uint32_t MmioSize() const override { return 0x00000100u; }

protected:
    const char* Label() const override { return "CM_IVA2"; }

    const char* RegisterName(uint32_t off) const override {
        switch (off) {
        case 0x00: return "CM_FCLKEN_IVA2";
        case 0x04: return "CM_CLKEN_PLL_IVA2";
        case 0x20: return "CM_IDLEST_IVA2";
        case 0x24: return "CM_IDLEST_PLL_IVA2";
        case 0x34: return "CM_AUTOIDLE_PLL_IVA2";
        case 0x40: return "CM_CLKSEL1_PLL_IVA2";
        case 0x44: return "CM_CLKSEL2_PLL_IVA2";
        case 0x48: return "CM_CLKSTCTRL_IVA2";
        case 0x4C: return "CM_CLKSTST_IVA2";
        }
        return nullptr;
    }
};

}  /* namespace */

REGISTER_SERVICE(Omap3530CmIva2);
