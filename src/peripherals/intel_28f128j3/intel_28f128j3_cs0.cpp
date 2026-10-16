#include "intel_28f128j3.h"

#include <cerfcore/boards/board_context.h>
#include "../../boards/siemens_simpad/siemens_simpad_family_id.h"
#include <cerfcore/core/cerf_emulator.h>

namespace {

/* SIMpad nCS0 boot flash: one x16 28F128J3, 16 MB (Linux arch/arm mach-sa1100
   simpad CS0 = SZ_16M). */
class Intel28F128J3Cs0 : public Intel28F128J3 {
public:
    using Intel28F128J3::Intel28F128J3;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SiemensSimpad;
    }

    uint32_t MmioBase() const override { return 0x00000000u; }
    uint32_t MmioSize() const override { return 0x01000000u; }

protected:
    uint32_t Parallel()    const override { return 1u; }
    uint32_t DeviceWidth() const override { return 2u; }
};

}  /* namespace */

REGISTER_SERVICE(Intel28F128J3Cs0);
