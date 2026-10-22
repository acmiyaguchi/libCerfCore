#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../socs/imx28/imx28_pinctrl.h"
#include "sharp_brain_pw_sh4_family_id.h"

#include <cstdint>

namespace cerfcore {

namespace {

/* sharp_brain_pw_sh4 cspddk.dll DDKGpioReadDataPin c0604af8, nk.exe 80211780: one bit per call.
   Bank 0: edna2_MRSensor.dll pins 6, 7; wavedev2_BU26154.dll pin 24; sdhc.dll and nk.exe 80212fc0
   pin 5. Bank 4: keybd_EDNA2.dll KBD_ScanMatrix c0878678 pins 0-4, 6, 7. */
constexpr uint32_t kConsumed[5] = {0x010000E0u, 0u, 0u, 0u, 0x000000DFu};

class SharpBrainPwSh4DinReaders : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SharpBrainPwSh4;
    }

    void OnReady() override {
        emu_.Get<Imx28Pinctrl>().SetDinConsumedProvider(
            [](uint32_t bank) { return bank < 5u ? kConsumed[bank] : 0u; });
    }
};

}  /* namespace */

REGISTER_SERVICE(SharpBrainPwSh4DinReaders);

}
