#include <cerfcore/core/service.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "sharp_brain_pw_sh4_family_id.h"
#include <cerfcore/socs/guest_cpu_reset.h>
#include "../../socs/imx28/imx28_clkctrl.h"

#include <cstdint>

namespace cerfcore {

namespace {

/* sharp_brain_pw_sh4 nk.exe 8020ac44, 8020a7ec; RM 10.8.8, 23.4.3 */
constexpr uint32_t kXbusPa     = 0x80040070u;
constexpr uint32_t kBootXbus   = 0x00000001u;

class SharpBrainPwSh4BootClocks : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SharpBrainPwSh4;
    }

    void OnReady() override {
        Apply();
        emu_.Get<GuestCpuReset>().RegisterResetReleaseListener([this] { Apply(); });
    }

private:
    void Apply() { emu_.Get<Imx28Clkctrl>().WriteWord(kXbusPa, kBootXbus); }
};

}

REGISTER_SERVICE(SharpBrainPwSh4BootClocks);

}
