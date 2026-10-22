#include <cerfcore/core/cerf_emulator.h>
#include "imx28_saif.h"

namespace cerfcore {

namespace {

class Imx28Saif1 : public Imx28Saif {
public:
    explicit Imx28Saif1(CerfEmulator& emu) : Imx28Saif(emu, 1u, 0x80046000u) {}
};

}  /* namespace */

REGISTER_SERVICE(Imx28Saif1);

}
