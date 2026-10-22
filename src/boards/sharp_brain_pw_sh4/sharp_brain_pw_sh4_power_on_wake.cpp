#include <cerfcore/core/service.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "sharp_brain_pw_sh4_family_id.h"
#include <cerfcore/host/guest_deep_sleep.h>

namespace cerfcore {

namespace {

/* MCIMX28RM Rev. 2 section 11.3.4 (watchdog expiry powers the chip down), 11.3.3 (power-up
   begins from the off state); sharp_brain_pw_sh4 nk.exe 8020e558 suspends by WFI (802124d4). */
class SharpBrainPwSh4PowerOnWake : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SharpBrainPwSh4;
    }

    void OnReady() override { emu_.Get<GuestDeepSleep>().RegisterPowerOnWake(); }
};

}

REGISTER_SERVICE(SharpBrainPwSh4PowerOnWake);

}
