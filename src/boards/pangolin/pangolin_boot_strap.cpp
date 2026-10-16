#include <cerfcore/core/service.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "pangolin_family_id.h"
#include "../../socs/sa11xx/sa11xx_ppc.h"

namespace cerfcore {

namespace {

/* The G138 OAL samples PPC pin 7 (L_DD7, an input pre-LCD) as a boot-mode
   strap: nk.exe 0x8C143790 `TST PPSR,#0x80` - bit 7 high = normal boot, low =
   `MOV PC,#0x20000` recovery/reflash stub. A run-position device reads bit 7
   high, so drive the input pin high for the normal boot path. */
class PangolinBootStrap : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::Pangolin;
    }

    void OnReady() override {
        emu_.Get<Sa11xxPpc>().DriveInputPin(7u, true);
    }
};

}  /* namespace */

REGISTER_SERVICE(PangolinBootStrap);

}
