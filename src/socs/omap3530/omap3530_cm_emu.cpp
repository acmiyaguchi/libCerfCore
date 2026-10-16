#include "omap3530_prcm_stub_block.h"

namespace {

class Omap3530CmEmu : public Omap3530PrcmStubBlock {
public:
    using Omap3530PrcmStubBlock::Omap3530PrcmStubBlock;

    uint32_t MmioBase() const override { return 0x48005100u; }
    uint32_t MmioSize() const override { return 0x00000100u; }

protected:
    const char* Label() const override { return "CM_EMU"; }

    const char* RegisterName(uint32_t off) const override {
        switch (off) {
        case 0x40: return "CM_CLKSEL1_EMU";
        case 0x48: return "CM_CLKSTCTRL_EMU";
        case 0x4C: return "CM_CLKSTST_EMU";
        case 0x50: return "CM_CLKSEL2_EMU";
        case 0x54: return "CM_CLKSEL3_EMU";
        }
        return nullptr;
    }
};

}  /* namespace */

REGISTER_SERVICE(Omap3530CmEmu);
