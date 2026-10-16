#include "../../socs/s3c2410/s3c2410_clock_input.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "samsung_smdk2410_family_id.h"

namespace {

/* S3C2410A UM p.7-20: the PLL value selection table is tabulated at Input
   Frequency 12.00MHz, and devemu_wm2003se nk.exe start's MPLLCON 0xA1031 /
   UPLLCON 0x48032 yield its 202.80MHz row and p.7-19's 48.00MHz UPLL. */
class SamsungSmdk2410ClockInput : public S3C2410ClockInput {
public:
    using S3C2410ClockInput::S3C2410ClockInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SamsungSmdk2410;
    }

    uint64_t FinHz() const override { return 12000000u; }
};

}

REGISTER_SERVICE_AS(SamsungSmdk2410ClockInput, S3C2410ClockInput);
