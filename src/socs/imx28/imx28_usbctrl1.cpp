#include <cerfcore/core/cerf_emulator.h>
#include "imx28_usbctrl.h"

namespace cerfcore {

namespace {

class Imx28Usbctrl1 : public Imx28Usbctrl {
public:
    explicit Imx28Usbctrl1(CerfEmulator& emu) : Imx28Usbctrl(emu, 1u, 0x80090000u) {}
};

}  /* namespace */

REGISTER_SERVICE(Imx28Usbctrl1);

}
