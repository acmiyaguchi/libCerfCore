#include "../../cpu/vr5500/vr5500_board_clock.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "nec_sg2_vr5500_family_id.h"

namespace {

class NecSg2Vr5500Vr5500BoardClock : public Vr5500BoardClock {
public:
    using Vr5500BoardClock::Vr5500BoardClock;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::NecSg2Vr5500;
    }

    uint64_t PClockHz() const override { return 400000000u; }
};

}

REGISTER_SERVICE_AS(NecSg2Vr5500Vr5500BoardClock, Vr5500BoardClock);
