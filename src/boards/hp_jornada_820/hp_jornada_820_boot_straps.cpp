#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../socs/sa11xx/sa11xx_boot_straps.h"
#include "hp_jornada_820_family_id.h"

namespace cerfcore {

namespace {

/* Bundle jornada820, nk.exe memory controller setup: MSC0 0x8E605258 (bank 0 RT0 = 0,
   RBW0 = 0). SA-1100 TRM §10.2.3: RT 00 is "Nonburst ROM or Flash EPROM". */
class HpJornada820BootStraps : public Sa11xxBootStraps {
public:
    using Sa11xxBootStraps::Sa11xxBootStraps;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::HpJornada820;
    }

    bool RomSelHigh() const override  { return true; }
    bool SmromEnHigh() const override { return false; }
};

}

REGISTER_SERVICE_AS(HpJornada820BootStraps, Sa11xxBootStraps);

}
