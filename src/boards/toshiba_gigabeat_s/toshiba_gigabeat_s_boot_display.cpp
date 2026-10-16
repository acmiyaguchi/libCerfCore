#include <cerfcore/boards/board_context.h>
#include "toshiba_gigabeat_s_family_id.h"
#include "toshiba_gigabeat_s_framebuffer.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/core/service.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "../../socs/imx31/imx31_ipu.h"

namespace {

class ToshibaGigabeatSBootDisplay : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::ToshibaGigabeatS;
    }

    void OnReady() override {
        SetupScanout();
        emu_.Get<GuestCpuReset>().RegisterResetReleaseListener([this] { SetupScanout(); });
    }

private:
    void SetupScanout() {
        emu_.Get<Imx31Ipu>().SetupSdcScanout(
            toshiba_gigabeat_s::kFbPa, toshiba_gigabeat_s::kScreenW, toshiba_gigabeat_s::kScreenH);
        LOG(Board, "ToshibaGigabeatSBootDisplay: IPU SDC scan-out %ux%u RGB565 @ PA 0x%08X\n",
            toshiba_gigabeat_s::kScreenW, toshiba_gigabeat_s::kScreenH, toshiba_gigabeat_s::kFbPa);
    }
};

}  /* namespace */

REGISTER_SERVICE(ToshibaGigabeatSBootDisplay);
