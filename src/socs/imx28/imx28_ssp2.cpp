#include <cerfcore/core/cerf_emulator.h>
#include "imx28_ssp.h"

namespace cerfcore {

namespace {

/* MCIMX28RM Rev. 2 section 17.10 SSP2 base 0x80014000; Table 5-1 source 98 ssp2_error_irq. */
class Imx28Ssp2 : public Imx28Ssp {
public:
    explicit Imx28Ssp2(CerfEmulator& emu) : Imx28Ssp(emu, 2u, 0x80014000u, 98u) {}
};

}  /* namespace */

REGISTER_SERVICE(Imx28Ssp2);

}
