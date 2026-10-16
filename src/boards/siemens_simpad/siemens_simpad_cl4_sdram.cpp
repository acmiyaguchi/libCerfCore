#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../socs/sa11xx/sa1110_sdram_geometry.h"
#include "simpad_cl4_id.h"
#include "swisscom_wp50_id.h"
#include "t_sinus_pad_id.h"

namespace cerfcore {

namespace {

class SiemensSimpadCl4Sdram : public Sa1110SdramGeometry {
public:
    using Sa1110SdramGeometry::Sa1110SdramGeometry;

    bool ShouldRegister() override {
        const auto board = emu_.Get<BoardContext>().GetBoardId();
        return board == BoardId::SimpadCl4 || board == BoardId::TSinusPad ||
               board == BoardId::SwisscomWp50;
    }

    uint32_t RowBits() const override    { return 12; }
    uint32_t ColumnBits() const override { return 9; }
    uint32_t BankBits() const override   { return 2; }
};

}

REGISTER_SERVICE_AS(SiemensSimpadCl4Sdram, Sa1110SdramGeometry);

}
