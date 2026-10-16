#include <cerfcore/core/service.h>

#include <cerfcore/core/cerf_emulator.h>
#include "../../socs/vr41xx/vr41xx_giu.h"
#include <cerfcore/boards/board_context.h>
#include "nec_cronos3e_family_id.h"

namespace cerfcore {

namespace {

/* nec_mobilepro_790_hpc2000 nk.exe start @0x9E0999E8 boots on GIUPIODL D12 = 1; with it 0,
   sub_9E09B554 shows "Slide the battery release lock to the locked position!" and polls D12
   (VR4121 UM U13569EJ2V0UM00 19.2.3). */
constexpr int kBatteryLockPin = 12;

class NecCronos3eBatteryLock : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::NecCronos3e;
    }

    void OnReady() override {
        emu_.Get<Vr41xxGiu>().SetPinLevel(kBatteryLockPin, true);
    }
};

}

REGISTER_SERVICE(NecCronos3eBatteryLock);

}
