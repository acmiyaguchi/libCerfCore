#include <cerfcore/core/service.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "sharp_brain_pw_sh4_family_id.h"
#include "../../state/state_boot_gate.h"
#include <cerfcore/socs/guest_cpu_reset.h>
#include "../../peripherals/peripheral_dispatcher.h"

#include <cstdint>

namespace cerfcore {

namespace {

/* sharp_brain_pw_sh4 SDU item 3 boot stream 0001e504, 0001e51c; RM 28.4.1, 39.3 */
constexpr uint32_t kPwmCtrlClrPa = 0x80064008u;
constexpr uint32_t kSftrst       = 0x80000000u;
constexpr uint32_t kClkgate      = 0x40000000u;

class SharpBrainPwSh4BootPwm : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SharpBrainPwSh4;
    }

    void OnReady() override {
        emu_.Get<StateBootGate>().RegisterColdHandoff([this] { Apply(); });
        emu_.Get<GuestCpuReset>().RegisterResetReleaseListener([this] { Apply(); });
    }

private:
    void Apply() {
        auto& bus = emu_.Get<PeripheralDispatcher>();
        bus.WriteWord(kPwmCtrlClrPa, kSftrst);
        bus.WriteWord(kPwmCtrlClrPa, kClkgate);
    }
};

}

REGISTER_SERVICE(SharpBrainPwSh4BootPwm);

}
