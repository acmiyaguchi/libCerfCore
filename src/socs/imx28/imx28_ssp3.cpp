#include <cerfcore/core/cerf_emulator.h>
#include "imx28_ssp.h"

namespace cerfcore {

namespace {

/* MCIMX28RM Rev. 2 section 17.10 SSP3 base 0x80016000; Table 5-1 source 99 ssp3_error_irq. */
class Imx28Ssp3 : public Imx28Ssp {
public:
    explicit Imx28Ssp3(CerfEmulator& emu) : Imx28Ssp(emu, 3u, 0x80016000u, 99u) {}
};

}  /* namespace */

REGISTER_SERVICE(Imx28Ssp3);

}
