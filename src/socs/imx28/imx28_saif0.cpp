#include <cerfcore/core/cerf_emulator.h>
#include "imx28_saif.h"

namespace cerfcore {

namespace {

class Imx28Saif0 : public Imx28Saif {
public:
    explicit Imx28Saif0(CerfEmulator& emu) : Imx28Saif(emu, 0u, 0x80042000u) {}
};

}  /* namespace */

REGISTER_SERVICE(Imx28Saif0);

}
