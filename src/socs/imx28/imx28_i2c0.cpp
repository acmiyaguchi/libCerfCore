#include <cerfcore/core/cerf_emulator.h>
#include "imx28_i2c.h"

namespace cerfcore {

namespace {

/* MCIMX28RM Rev. 2 section 27.5 I2C0 base 0x80058000; Table 5-1 source 111 i2c0_error_irq;
   Table 7-1 APBX channel 6 I2C0. */
class Imx28I2c0 : public Imx28I2c {
public:
    explicit Imx28I2c0(CerfEmulator& emu) : Imx28I2c(emu, 0x80058000u, 111u, 6u, true) {}
};

}  /* namespace */

REGISTER_SERVICE(Imx28I2c0);

}
