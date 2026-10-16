#include "../../socs/s3c2410/s3c2410_clock_input.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "siemens_simatic_tp177b_4_family_id.h"

namespace cerfcore {

namespace {

/* siemens_tp177b_4inch_v1020 nk.exe sub_8303EFC8: the OAL derives FCLK
   from MPLLCON with a 12,000,000 Hz input. */
class SiemensSimaticTp177b4ClockInput : public S3C2410ClockInput {
public:
    using S3C2410ClockInput::S3C2410ClockInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SiemensSimaticTp177b4;
    }

    uint64_t FinHz() const override { return 12000000u; }
};

}

REGISTER_SERVICE_AS(SiemensSimaticTp177b4ClockInput, S3C2410ClockInput);

}
