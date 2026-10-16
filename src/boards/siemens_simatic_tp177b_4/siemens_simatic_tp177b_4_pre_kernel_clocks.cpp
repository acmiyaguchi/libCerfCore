#include "../../socs/s3c2410/s3c2410_pre_kernel_clocks.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "siemens_simatic_tp177b_4_family_id.h"

namespace {

constexpr uint32_t kMpllCon = 0x000A1031u;

/* HDIVN: siemens_tp177b_4inch_v1020 nk.exe StartUp 0x83008DDC enables ARM920T
   asynchronous clocking unconditionally, correct only when FCLK differs from
   HCLK. PDIVN: S3C2410A UM Table 24-5 caps PCLK at 50MHz / 66.5MHz. */
constexpr uint32_t kClkDivn = 0x00000003u;

class SiemensSimaticTp177b4PreKernelClocks : public S3C2410PreKernelClocks {
public:
    using S3C2410PreKernelClocks::S3C2410PreKernelClocks;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SiemensSimaticTp177b4;
    }

    uint32_t MpllCon() const override { return kMpllCon; }
    uint32_t ClkDivn() const override { return kClkDivn; }
};

}

REGISTER_SERVICE_AS(SiemensSimaticTp177b4PreKernelClocks, S3C2410PreKernelClocks);
