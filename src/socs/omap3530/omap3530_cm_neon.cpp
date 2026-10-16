#include "omap3530_prcm_stub_block.h"

namespace {

class Omap3530CmNeon : public Omap3530PrcmStubBlock {
public:
    using Omap3530PrcmStubBlock::Omap3530PrcmStubBlock;

    uint32_t MmioBase() const override { return 0x48005300u; }
    uint32_t MmioSize() const override { return 0x00000100u; }

protected:
    const char* Label() const override { return "CM_NEON"; }

    const char* RegisterName(uint32_t off) const override {
        switch (off) {
        case 0x20: return "CM_IDLEST_NEON";
        case 0x48: return "CM_CLKSTCTRL_NEON";
        }
        return nullptr;
    }
};

}  /* namespace */

REGISTER_SERVICE(Omap3530CmNeon);
