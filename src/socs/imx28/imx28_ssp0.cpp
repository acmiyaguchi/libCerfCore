#include <cerfcore/core/cerf_emulator.h>
#include "imx28_ssp.h"

namespace cerfcore {

namespace {

/* MCIMX28RM Rev. 2 section 17.10 SSP0 base 0x80010000; Table 5-1 source 96 ssp0_error_irq. */
class Imx28Ssp0 : public Imx28Ssp {
public:
    explicit Imx28Ssp0(CerfEmulator& emu) : Imx28Ssp(emu, 0u, 0x80010000u, 96u) {}
};

}  /* namespace */

REGISTER_SERVICE(Imx28Ssp0);

}
