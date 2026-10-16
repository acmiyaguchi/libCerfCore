#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../socs/sa11xx/sa1110_sdram_geometry.h"
#include "compaq_ipaq_h3000_family_id.h"

namespace {

/* Samsung K4S281632B datasheet, pin function description: "Row address : RA0 ~
   RA11, Column address : CA0 ~ CA8", BA0 ~ BA1. Linux drivers/cpufreq/
   sa1110-cpufreq.c sa1110_clk_init: machine_is_h3600() uses "K4S281632B-1H". */
class CompaqIpaqH3000K4s281632bSdram : public Sa1110SdramGeometry {
public:
    using Sa1110SdramGeometry::Sa1110SdramGeometry;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() ==
               BoardFamilyId::CompaqIpaqH3000;
    }

    uint32_t RowBits() const override    { return 12; }
    uint32_t ColumnBits() const override { return 9; }
    uint32_t BankBits() const override   { return 2; }
};

}

REGISTER_SERVICE_AS_FALLBACK(CompaqIpaqH3000K4s281632bSdram, Sa1110SdramGeometry);
