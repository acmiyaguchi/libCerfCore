#include <cerfcore/core/cerf_emulator.h>
#include "imx28_i2c.h"

namespace cerfcore {

namespace {

/* MCIMX28RM Rev. 2 Table 2-1 I2C1 base 0x8005A000; Table 5-1 source 110 i2c1_error_irq;
   Table 7-1 APBX channel 7 I2C1. */
class Imx28I2c1 : public Imx28I2c {
public:
    explicit Imx28I2c1(CerfEmulator& emu) : Imx28I2c(emu, 0x8005A000u, 110u, 7u, false) {}
};

}  /* namespace */

REGISTER_SERVICE(Imx28I2c1);

}
