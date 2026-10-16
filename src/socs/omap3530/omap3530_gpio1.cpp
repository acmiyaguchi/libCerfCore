#include "omap3530_prcm_stub_block.h"

namespace {

class Omap3530Gpio1 : public Omap3530GpioBankBase {
public:
    using Omap3530GpioBankBase::Omap3530GpioBankBase;
    uint32_t MmioBase()  const override { return 0x48310000u; }
    uint32_t BankIndex() const override { return 0u; }
    int      IrqNumber() const override { return 29; }
protected:
    const char* Label() const override { return "GPIO1"; }
};

}  /* namespace */

REGISTER_SERVICE(Omap3530Gpio1);
