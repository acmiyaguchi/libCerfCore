#include "omap3530_prcm_stub_block.h"

namespace {

class Omap3530CmOcpSystem : public Omap3530PrcmStubBlock {
public:
    using Omap3530PrcmStubBlock::Omap3530PrcmStubBlock;

    uint32_t MmioBase() const override { return 0x48004800u; }
    uint32_t MmioSize() const override { return 0x00000100u; }

protected:
    const char* Label() const override { return "CM_OCP_SYSTEM"; }

    const char* RegisterName(uint32_t off) const override {
        switch (off) {
        case 0x00: return "CM_REVISION";
        case 0x10: return "CM_SYSCONFIG";
        }
        return nullptr;
    }
};

}  /* namespace */

REGISTER_SERVICE(Omap3530CmOcpSystem);
