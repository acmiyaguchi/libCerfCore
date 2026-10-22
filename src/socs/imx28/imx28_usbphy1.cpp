#include <cerfcore/core/cerf_emulator.h>
#include "imx28_usbphy.h"

namespace cerfcore {

namespace {

class Imx28Usbphy1 : public Imx28Usbphy {
public:
    explicit Imx28Usbphy1(CerfEmulator& emu) : Imx28Usbphy(emu, 1u, 0x8007E000u) {}
};

}  /* namespace */

REGISTER_SERVICE(Imx28Usbphy1);

}
