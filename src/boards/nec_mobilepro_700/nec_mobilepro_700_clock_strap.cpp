#include "../../socs/vr41xx/vr41xx_clock_strap.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "nec_mobilepro_700_family_id.h"

namespace cerfcore {

namespace {

class NecMobilepro700ClockStrap : public Vr41xxClockStrap {
public:
    using Vr41xxClockStrap::Vr41xxClockStrap;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::NecMobilepro700;
    }

    uint32_t Clksel() const override { return 0x5u; }
};

}

REGISTER_SERVICE_AS(NecMobilepro700ClockStrap, Vr41xxClockStrap);

}
