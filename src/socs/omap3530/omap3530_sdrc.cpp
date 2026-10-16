#include "omap3530_prcm_stub_block.h"

namespace {

class Omap3530Sdrc : public Omap3530PrcmStubBlock {
public:
    using Omap3530PrcmStubBlock::Omap3530PrcmStubBlock;

    uint32_t MmioBase() const override { return 0x6D000000u; }
    uint32_t MmioSize() const override { return 0x00001000u; }

protected:
    const char* Label() const override { return "SDRC"; }

    const char* RegisterName(uint32_t off) const override {
        switch (off) {
        case 0x00: return "SDRC_REVISION";
        case 0x10: return "SDRC_SYSCONFIG";
        case 0x14: return "SDRC_STATUS";
        case 0x40: return "SDRC_CS_CFG";
        case 0x44: return "SDRC_SHARING";
        case 0x48: return "SDRC_ERR_ADDR";
        case 0x4C: return "SDRC_ERR_TYPE";
        case 0x60: return "SDRC_DLLA_CTRL";
        case 0x64: return "SDRC_DLLA_STATUS";
        case 0x68: return "SDRC_DLLB_CTRL";
        case 0x6C: return "SDRC_DLLB_STATUS";
        case 0x70: return "SDRC_POWER";
        case 0x80: return "SDRC_MCFG_0";
        case 0x84: return "SDRC_MR_0";
        case 0x88: return "SDRC_EMR1_0";
        case 0x8C: return "SDRC_EMR2_0";
        case 0x94: return "SDRC_DCDL1_CTRL";
        case 0x98: return "SDRC_DCDL2_CTRL";
        case 0x9C: return "SDRC_ACTIM_CTRLA_0";
        case 0xA0: return "SDRC_ACTIM_CTRLB_0";
        case 0xA4: return "SDRC_RFR_CTRL_0";
        case 0xA8: return "SDRC_MANUAL_0";
        case 0xB0: return "SDRC_MCFG_1";
        case 0xB4: return "SDRC_MR_1";
        case 0xB8: return "SDRC_EMR1_1";
        case 0xBC: return "SDRC_EMR2_1";
        case 0xC4: return "SDRC_ACTIM_CTRLA_1";
        case 0xC8: return "SDRC_ACTIM_CTRLB_1";
        case 0xD4: return "SDRC_RFR_CTRL_1";
        case 0xD8: return "SDRC_MANUAL_1";
        }
        return nullptr;
    }
};

}  /* namespace */

REGISTER_SERVICE(Omap3530Sdrc);
