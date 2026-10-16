#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../socs/sa11xx/sa11xx_boot_straps.h"
#include "compaq_ipaq_h3000_family_id.h"

namespace {

/* handhelds.org bootldr 2.18.01: btflash.c btflash_init "flash being 16 bit wide -->
   monochrome iPAQ"; boot-sa1100.s writes SMCNFG 0, "we have none". Bundle
   ipaq_h3600_ppc2002 nk.exe table 0x80041248: MSC0 0x46904690 (RBW0 = 0). */
class CompaqIpaqH3000BootStraps : public Sa11xxBootStraps {
public:
    using Sa11xxBootStraps::Sa11xxBootStraps;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() ==
               BoardFamilyId::CompaqIpaqH3000;
    }

    bool RomSelHigh() const override  { return true; }
    bool SmromEnHigh() const override { return false; }
};

}

REGISTER_SERVICE_AS_FALLBACK(CompaqIpaqH3000BootStraps, Sa11xxBootStraps);
