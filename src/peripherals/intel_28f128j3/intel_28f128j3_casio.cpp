#include "intel_28f128j3.h"

#include <cerfcore/boards/board_context.h>
#include "../../boards/casio_pocketpostpet/casio_pocketpostpet_family_id.h"
#include <cerfcore/core/cerf_emulator.h>

namespace {

/* Casio Toricomail ROM: one x16 28F128J3 at PA 0x1F000000 (VR4121 ROMCS, 16 MB).
   MMCRestore.exe write-to-buffer (sub_131F8) + block-base Read-ID (sub_130E8). */
class Intel28F128J3Casio : public Intel28F128J3 {
public:
    using Intel28F128J3::Intel28F128J3;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::CasioPocketpostpet;
    }

    uint32_t MmioBase() const override { return 0x1F000000u; }
    uint32_t MmioSize() const override { return 0x01000000u; }

protected:
    uint32_t Parallel()    const override { return 1u; }
    uint32_t DeviceWidth() const override { return 2u; }
};

}  /* namespace */

REGISTER_SERVICE(Intel28F128J3Casio);
