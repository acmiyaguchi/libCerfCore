#include "../../socs/vr41xx/vr41xx_clock_strap.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "casio_jx670_family_id.h"

namespace {

class CasioJx670ClockStrap : public Vr41xxClockStrap {
public:
    using Vr41xxClockStrap::Vr41xxClockStrap;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::CasioJx670;
    }

    uint32_t Clksel() const override { return 0x4u; }
};

}

REGISTER_SERVICE_AS(CasioJx670ClockStrap, Vr41xxClockStrap);
