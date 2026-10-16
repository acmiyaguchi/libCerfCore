#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../socs/sa11xx/sa11xx_boot_straps.h"
#include "ipaq_h31xx_id.h"

namespace {

/* Bundle ipaq_h3100_ppc2002, nk.exe start table at 0x80041248: MSC0 0x46944694
   (bank 0 RT0 = 0, RBW0 = 1) and SMCNFG 0xAFCCAFCC (SM0 = 0). */
class CompaqIpaqH31xxBootStraps : public Sa11xxBootStraps {
public:
    using Sa11xxBootStraps::Sa11xxBootStraps;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardId() == BoardId::IpaqH31xx;
    }

    bool RomSelHigh() const override  { return false; }
    bool SmromEnHigh() const override { return false; }
};

}

REGISTER_SERVICE_AS(CompaqIpaqH31xxBootStraps, Sa11xxBootStraps);
