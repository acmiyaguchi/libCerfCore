#include "omap3530_prcm_stub_block.h"

namespace {

class Omap3530PrmOcpSystem : public Omap3530PrcmStubBlock {
public:
    using Omap3530PrcmStubBlock::Omap3530PrcmStubBlock;

    uint32_t MmioBase() const override { return 0x48306800u; }
    uint32_t MmioSize() const override { return 0x00000100u; }

protected:
    const char* Label() const override { return "PRM_OCP_SYSTEM"; }

    const char* RegisterName(uint32_t off) const override {
        switch (off) {
        case 0x04: return "PRM_REVISION";
        case 0x14: return "PRM_SYSCONFIG";
        case 0x18: return "PRM_IRQSTATUS_MPU";
        case 0x1C: return "PRM_IRQENABLE_MPU";
        }
        return nullptr;
    }
};

}  /* namespace */

REGISTER_SERVICE(Omap3530PrmOcpSystem);
