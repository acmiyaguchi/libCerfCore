#include "../../socs/imx31/imx31_clock_input.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "toshiba_gigabeat_s_family_id.h"

namespace cerfcore {

namespace {

class ToshibaGigabeatSClockInput : public Imx31ClockInput {
public:
    using Imx31ClockInput::Imx31ClockInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::ToshibaGigabeatS;
    }

    uint64_t CkihHz() const override { return 27000000u; }
    uint64_t CkilHz() const override { return 32768u; }
    /* zune_keel EBoot.bin nk.exe start() 0x880421C8: the first CCMR write sets PRCS CKIH with
       MPE set; MCIMX31RM Table 3-4 PRCS "can be modified only when the MCU PLL is disabled". */
    bool ResetReferenceIsCkih() const override { return true; }
};

}

REGISTER_SERVICE_AS(ToshibaGigabeatSClockInput, Imx31ClockInput);

}
