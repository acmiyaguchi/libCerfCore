#include "../../socs/pr31x00/pr31x00_clock_crystal.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "sharp_mobilon_hc_family_id.h"

namespace cerfcore {

namespace {

class SharpMobilonHcClockCrystal : public Pr31x00ClockCrystal {
public:
    using Pr31x00ClockCrystal::Pr31x00ClockCrystal;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::SharpMobilonHc;
    }

    uint64_t FinHz() const override { return 9216000u; }
};

}

REGISTER_SERVICE_AS(SharpMobilonHcClockCrystal, Pr31x00ClockCrystal);

}
