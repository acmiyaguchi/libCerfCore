#include <cerfcore/core/cerf_emulator.h>
#include "imx28_usbphy.h"

namespace cerfcore {

namespace {

class Imx28Usbphy0 : public Imx28Usbphy {
public:
    explicit Imx28Usbphy0(CerfEmulator& emu) : Imx28Usbphy(emu, 0u, 0x8007C000u) {}
};

}  /* namespace */

REGISTER_SERVICE(Imx28Usbphy0);

}
