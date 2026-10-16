#include "omap3530_prcm_stub_block.h"

namespace {

class Omap3530Gpio2 : public Omap3530GpioBankBase {
public:
    using Omap3530GpioBankBase::Omap3530GpioBankBase;
    uint32_t MmioBase()  const override { return 0x49050000u; }
    uint32_t BankIndex() const override { return 1u; }
    int      IrqNumber() const override { return 30; }
protected:
    const char* Label() const override { return "GPIO2"; }
};

}  /* namespace */

REGISTER_SERVICE(Omap3530Gpio2);
