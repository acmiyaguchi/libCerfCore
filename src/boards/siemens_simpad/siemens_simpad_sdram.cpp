#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../socs/sa11xx/sa1110_sdram_geometry.h"
#include "siemens_simpad_family_id.h"

namespace {

class SiemensSimpadSdram : public Sa1110SdramGeometry {
public:
    using Sa1110SdramGeometry::Sa1110SdramGeometry;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::SiemensSimpad;
    }

    uint32_t RowBits() const override    { return 13; }
    uint32_t ColumnBits() const override { return 9; }
    uint32_t BankBits() const override   { return 2; }
};

}

REGISTER_SERVICE_AS_FALLBACK(SiemensSimpadSdram, Sa1110SdramGeometry);
