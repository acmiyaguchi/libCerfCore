#include "omap3530_prcm_stub_block.h"

namespace {

class Omap3530CmGlobal : public Omap3530PrcmStubBlock {
public:
    using Omap3530PrcmStubBlock::Omap3530PrcmStubBlock;

    uint32_t MmioBase() const override { return 0x48005200u; }
    uint32_t MmioSize() const override { return 0x00000100u; }

protected:
    const char* Label() const override { return "CM_GLOBAL"; }

    const char* RegisterName(uint32_t off) const override {
        switch (off) {
        case 0x9C: return "CM_POLCTRL";
        }
        return nullptr;
    }
};

}  /* namespace */

REGISTER_SERVICE(Omap3530CmGlobal);
