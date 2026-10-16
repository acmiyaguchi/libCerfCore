#include <cerfcore/core/cerf_emulator.h>
#include "../../peripherals/freescale_mc13783/mc13783.h"
#include "../../socs/imx31/imx31_wdog_pin.h"
#include <cerfcore/boards/board_context.h>
#include "toshiba_gigabeat_s_family_id.h"

namespace {

/* zune_keel EBoot.bin nk.exe sub_880673BC: sets the MC13783 alarm 3 s ahead, clears TODAM (register 4 =
   0xFFE7C5), then sub_88066704 clears WCR WDA; MC13783 UG page 5-11: "The phone is powered off upon
   software initiative based on this interrupt, by pulling WDI low." */
class ToshibaGigabeatSWdogWdi : public Imx31WdogPin {
public:
    using Imx31WdogPin::Imx31WdogPin;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::ToshibaGigabeatS;
    }

    void SetWdogAsserted(bool asserted) override { emu_.Get<Mc13783>().SetWdi(!asserted); }
};

}

REGISTER_SERVICE_AS(ToshibaGigabeatSWdogWdi, Imx31WdogPin);
