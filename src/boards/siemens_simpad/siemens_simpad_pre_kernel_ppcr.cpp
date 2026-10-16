#include "../../socs/sa11xx/sa11xx_pre_kernel_ppcr.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "siemens_simpad_family_id.h"

namespace {

class SiemensSimpadPreKernelPpcr : public Sa11xxPreKernelPpcr {
public:
    using Sa11xxPreKernelPpcr::Sa11xxPreKernelPpcr;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SiemensSimpad;
    }

    /* SA-1110 Developer's Manual §8.2 Table 8-1: CCF 01010 selects
       206.4 MHz on the 3.6864-MHz crystal. */
    uint32_t PpcrValue() const override { return 0x0000000Au; }
};

}

REGISTER_SERVICE_AS(SiemensSimpadPreKernelPpcr, Sa11xxPreKernelPpcr);
