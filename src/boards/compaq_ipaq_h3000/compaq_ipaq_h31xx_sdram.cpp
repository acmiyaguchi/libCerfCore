#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../socs/sa11xx/sa1110_sdram_geometry.h"
#include "ipaq_h31xx_id.h"

namespace {

/* handhelds.org bootldr 2.18.01 boot-sa1100.s dram_mdcnfg_16mb: "KM416S4030C, 12 row
   address bits, 8 col address bits". */
class CompaqIpaqH31xxSdram : public Sa1110SdramGeometry {
public:
    using Sa1110SdramGeometry::Sa1110SdramGeometry;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardId() == BoardId::IpaqH31xx;
    }

    uint32_t RowBits() const override    { return 12; }
    uint32_t ColumnBits() const override { return 8; }
    uint32_t BankBits() const override   { return 2; }
};

}

REGISTER_SERVICE_AS(CompaqIpaqH31xxSdram, Sa1110SdramGeometry);
