#include "../../socs/vr41xx/vr41xx_clock_strap.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "nec_cronos3e_family_id.h"

namespace cerfcore {

namespace {

class NecCronos3eClockStrap : public Vr41xxClockStrap {
public:
    using Vr41xxClockStrap::Vr41xxClockStrap;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::NecCronos3e;
    }

    uint32_t Clksel() const override { return 0x6u; }
};

}

REGISTER_SERVICE_AS(NecCronos3eClockStrap, Vr41xxClockStrap);

}
