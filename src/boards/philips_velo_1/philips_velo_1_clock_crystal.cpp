#include "../../socs/pr31x00/pr31x00_clock_crystal.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "philips_velo_1_family_id.h"

namespace {

class PhilipsVelo1ClockCrystal : public Pr31x00ClockCrystal {
public:
    using Pr31x00ClockCrystal::Pr31x00ClockCrystal;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::PhilipsVelo1;
    }

    uint64_t FinHz() const override { return 9216000u; }
};

}

REGISTER_SERVICE_AS(PhilipsVelo1ClockCrystal, Pr31x00ClockCrystal);
