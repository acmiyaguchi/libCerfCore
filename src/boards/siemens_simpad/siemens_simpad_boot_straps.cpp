#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../socs/sa11xx/sa11xx_boot_straps.h"
#include "siemens_simpad_family_id.h"

namespace {

/* Bundle simpad_sl4_ce4_10, nk.exe sub_800B4968 / sub_800B47A4: the CS0 flash ID
   (base 0xA4000000) is read with 16-bit accesses and must be 0x18 or 0x17. */
class SiemensSimpadBootStraps : public Sa11xxBootStraps {
public:
    using Sa11xxBootStraps::Sa11xxBootStraps;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::SiemensSimpad;
    }

    bool RomSelHigh() const override  { return false; }
    bool SmromEnHigh() const override { return false; }
};

}

REGISTER_SERVICE_AS(SiemensSimpadBootStraps, Sa11xxBootStraps);
