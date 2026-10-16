#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../socs/sa11xx/sa11xx_boot_straps.h"
#include "hp_jornada_700_family_id.h"

namespace cerfcore {

namespace {

/* HP Jornada 720 Hardware Configuration v0.20, Memory Controller: "MSC0 0xFFF04F78"
   (bank 0 RT0 = 0, RBW0 = 0). Linux arch/arm/mach-sa1100/jornada720.c registers
   CS0 as "cfi_probe" flash. */
class HpJornada700BootStraps : public Sa11xxBootStraps {
public:
    using Sa11xxBootStraps::Sa11xxBootStraps;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::HpJornada700;
    }

    bool RomSelHigh() const override  { return true; }
    bool SmromEnHigh() const override { return false; }
};

}

REGISTER_SERVICE_AS(HpJornada700BootStraps, Sa11xxBootStraps);

}
