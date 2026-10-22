#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../socs/imx28/imx28_rtc_crystal_fuses.h"
#include "sharp_brain_pw_sh4_family_id.h"

namespace cerfcore {

namespace {

/* MCIMX28RM Rev. 2 section 22.8.7 XTAL32_FREQ [6] ("the 32kHz crystal"); sharp_brain_pw_sh4
   nk.exe FUN_802101ec (802101ec) selects CLOCKSOURCE 1 with XTAL32_FREQ 0, the 32.768 kHz
   crystal. */
class SharpBrainPwSh4RtcCrystalFuses : public Imx28RtcCrystalFuses {
public:
    using Imx28RtcCrystalFuses::Imx28RtcCrystalFuses;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SharpBrainPwSh4;
    }

    bool Xtal32768Present() override { return true; }
    bool Xtal32000Present() override { return false; }
};

}

REGISTER_SERVICE_AS(SharpBrainPwSh4RtcCrystalFuses, Imx28RtcCrystalFuses);

}
