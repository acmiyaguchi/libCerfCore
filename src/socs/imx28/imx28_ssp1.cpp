#include <cerfcore/core/cerf_emulator.h>
#include "imx28_ssp.h"

namespace cerfcore {

namespace {

/* MCIMX28RM Rev. 2 section 17.10 SSP1 base 0x80012000; Table 5-1 source 97 ssp1_error_irq. */
class Imx28Ssp1 : public Imx28Ssp {
public:
    explicit Imx28Ssp1(CerfEmulator& emu) : Imx28Ssp(emu, 1u, 0x80012000u, 97u) {}
};

}  /* namespace */

REGISTER_SERVICE(Imx28Ssp1);

}
