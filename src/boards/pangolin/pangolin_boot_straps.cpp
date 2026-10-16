#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../socs/sa11xx/sa11xx_boot_straps.h"
#include "pangolin_family_id.h"

namespace {

/* Bundle smartbook_g138_ce4_1, nk.exe memory controller setup: MSC0 0x4B954B90
   (bank 0 RT0 = 0, RBW0 = 0) and SMCNFG 0xAFCCAFCC (SM0 = 0). */
class PangolinBootStraps : public Sa11xxBootStraps {
public:
    using Sa11xxBootStraps::Sa11xxBootStraps;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::Pangolin;
    }

    bool RomSelHigh() const override  { return true; }
    bool SmromEnHigh() const override { return false; }
};

}

REGISTER_SERVICE_AS(PangolinBootStraps, Sa11xxBootStraps);
