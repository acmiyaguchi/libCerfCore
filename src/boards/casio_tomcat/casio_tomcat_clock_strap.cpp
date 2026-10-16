#include "../../socs/vr41xx/vr41xx_clock_strap.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "casio_tomcat_family_id.h"

namespace {

class CasioTomcatClockStrap : public Vr41xxClockStrap {
public:
    using Vr41xxClockStrap::Vr41xxClockStrap;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::CasioTomcat;
    }

    uint32_t Clksel() const override { return 0x3u; }
};

}

REGISTER_SERVICE_AS(CasioTomcatClockStrap, Vr41xxClockStrap);
