#include "omap3530_prcm_stub_block.h"

namespace {

class Omap3530PrmUsbhost : public Omap3530PrmDomainBlock {
public:
    using Omap3530PrmDomainBlock::Omap3530PrmDomainBlock;

    uint32_t MmioBase() const override { return 0x48307400u; }
    uint32_t MmioSize() const override { return 0x00000100u; }

protected:
    const char* Label() const override { return "PRM_USBHOST"; }

    const char* RegisterName(uint32_t off) const override {
        switch (off) {
        case 0x58: return "RM_RSTST_USBHOST";
        case 0xA0: return "PM_WKEN_USBHOST";
        case 0xA4: return "PM_MPUGRPSEL_USBHOST";
        case 0xA8: return "PM_IVA2GRPSEL_USBHOST";
        case 0xB0: return "PM_WKST_USBHOST";
        case 0xC8: return "PM_WKDEP_USBHOST";
        case 0xE0: return "PM_PWSTCTRL_USBHOST";
        case 0xE4: return "PM_PWSTST_USBHOST";
        case 0xE8: return "PM_PREPWSTST_USBHOST";
        }
        return nullptr;
    }
};

}  /* namespace */

REGISTER_SERVICE(Omap3530PrmUsbhost);
