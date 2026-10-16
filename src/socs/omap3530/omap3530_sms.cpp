#include "omap3530_prcm_stub_block.h"

namespace {

class Omap3530Sms : public Omap3530PrcmStubBlock {
public:
    using Omap3530PrcmStubBlock::Omap3530PrcmStubBlock;

    uint32_t MmioBase() const override { return 0x6C000000u; }
    uint32_t MmioSize() const override { return 0x00001000u; }

protected:
    const char* Label() const override { return "SMS"; }

    const char* RegisterName(uint32_t off) const override {
        switch (off) {
        case 0x000: return "SMS_REVISION";
        case 0x010: return "SMS_SYSCONFIG";
        case 0x014: return "SMS_SYSSTATUS";
        case 0x150: return "SMS_CLASS_ARBITER0";
        case 0x154: return "SMS_CLASS_ARBITER1";
        case 0x158: return "SMS_CLASS_ARBITER2";
        }
        return nullptr;
    }
};

}  /* namespace */

REGISTER_SERVICE(Omap3530Sms);
