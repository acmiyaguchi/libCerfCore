#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../socs/imx28/imx28_pinctrl.h"
#include "sharp_brain_pw_sh4_family_id.h"

#include <cstdint>

namespace cerfcore {

namespace {

struct PinLevel {
    uint32_t bank;
    uint32_t pin;
    bool     high;
};

/* sharp_brain_pw_sh4 edna2_MRSensor.dll c0891588: bank 0 pins 6, 7. wavedev2_BU26154.dll c07a9b68,
   c07a9bac: bank 0 pin 24, model string at PA 0x400EA110 not "SA3". keybd_EDNA2.dll
   KBD_ScanMatrix c0878678: bank 4 pins 0-4, 6, 7. */
constexpr PinLevel kLevels[] = {
    {0u, 5u, false},   /* sharp_brain_pw_sh4 sdhc.dll BSPSDHCIsCardPresent c078250c */
    {0u, 6u, false},
    {0u, 7u, true},
    {0u, 24u, true},
    {4u, 0u, true},
    {4u, 1u, true},
    {4u, 2u, true},
    {4u, 3u, true},
    {4u, 4u, true},
    {4u, 6u, true},
    {4u, 7u, true},
};

class SharpBrainPwSh4PinLevels : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SharpBrainPwSh4;
    }

    void OnReady() override {
        emu_.Get<Imx28Pinctrl>().SetPinLevelProvider(
            [](uint32_t bank, uint32_t pin, bool* high) {
                for (const PinLevel& l : kLevels) {
                    if (l.bank != bank || l.pin != pin) continue;
                    *high = l.high;
                    return true;
                }
                return false;
            });
    }
};

}  /* namespace */

REGISTER_SERVICE(SharpBrainPwSh4PinLevels);

}
