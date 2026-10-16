#include "../../socs/s3c2410/s3c2410_pre_kernel_clocks.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "samsung_smdk2410_family_id.h"

namespace cerfcore {

namespace {

/* devemu_wm2003se nk.exe start 0x800410A4: MPLLCON = 0xA1031;
   0x80041078: CLKDIVN = 3. */
constexpr uint32_t kMpllCon = 0x000A1031u;
constexpr uint32_t kClkDivn = 0x00000003u;

class SamsungSmdk2410PreKernelClocks : public S3C2410PreKernelClocks {
public:
    using S3C2410PreKernelClocks::S3C2410PreKernelClocks;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SamsungSmdk2410;
    }

    uint32_t MpllCon() const override { return kMpllCon; }
    uint32_t ClkDivn() const override { return kClkDivn; }
};

}

REGISTER_SERVICE_AS(SamsungSmdk2410PreKernelClocks, S3C2410PreKernelClocks);

}
