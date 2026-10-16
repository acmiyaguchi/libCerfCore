#include "omap3530_prcm_stub_block.h"

namespace {

class Omap3530CmCam : public Omap3530PrcmStubBlock {
public:
    using Omap3530PrcmStubBlock::Omap3530PrcmStubBlock;

    uint32_t MmioBase() const override { return 0x48004F00u; }
    uint32_t MmioSize() const override { return 0x00000100u; }

protected:
    const char* Label() const override { return "CM_CAM"; }

    const char* RegisterName(uint32_t off) const override {
        switch (off) {
        case 0x00: return "CM_FCLKEN_CAM";
        case 0x10: return "CM_ICLKEN_CAM";
        case 0x20: return "CM_IDLEST_CAM";
        case 0x30: return "CM_AUTOIDLE_CAM";
        case 0x40: return "CM_CLKSEL_CAM";
        case 0x44: return "CM_SLEEPDEP_CAM";
        case 0x48: return "CM_CLKSTCTRL_CAM";
        case 0x4C: return "CM_CLKSTST_CAM";
        }
        return nullptr;
    }
};

}  /* namespace */

REGISTER_SERVICE(Omap3530CmCam);
