#include "omap3530_prcm_stub_block.h"

namespace {

class Omap3530Gpio4 : public Omap3530GpioBankBase {
public:
    using Omap3530GpioBankBase::Omap3530GpioBankBase;
    uint32_t MmioBase()  const override { return 0x49054000u; }
    uint32_t BankIndex() const override { return 3u; }
    int      IrqNumber() const override { return 32; }
protected:
    const char* Label() const override { return "GPIO4"; }
};

}  /* namespace */

REGISTER_SERVICE(Omap3530Gpio4);
