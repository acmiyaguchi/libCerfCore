#include <cerfcore/core/cerf_emulator.h>
#include "imx28_usbctrl.h"

namespace cerfcore {

namespace {

class Imx28Usbctrl0 : public Imx28Usbctrl {
public:
    explicit Imx28Usbctrl0(CerfEmulator& emu) : Imx28Usbctrl(emu, 0u, 0x80080000u) {}
};

}  /* namespace */

REGISTER_SERVICE(Imx28Usbctrl0);

}
