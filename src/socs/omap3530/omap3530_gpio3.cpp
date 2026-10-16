#include "omap3530_prcm_stub_block.h"

namespace {

class Omap3530Gpio3 : public Omap3530GpioBankBase {
public:
    using Omap3530GpioBankBase::Omap3530GpioBankBase;
    uint32_t MmioBase()  const override { return 0x49052000u; }
    uint32_t BankIndex() const override { return 2u; }
    int      IrqNumber() const override { return 31; }
protected:
    const char* Label() const override { return "GPIO3"; }
};

}  /* namespace */

REGISTER_SERVICE(Omap3530Gpio3);
