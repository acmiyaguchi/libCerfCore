#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/keyboard_map.h>
#include "sharp_brain_pw_sh4_family_id.h"

#include <vector>

namespace cerfcore {

namespace {

/* device_code = HW_PWR_STS_PSWITCH level while held (MCIMX28RM Rev. 2 Table 11-1);
   sharp_brain_pw_sh4 keybd_EDNA2.dll KBD_ScanDebounced c08787a4 reads level 0x1 as key 0x1C. */
const std::vector<KeyBinding> kBindings = {
    { 0x7B, 0x1, "Power", 0, 0 },
};

class SharpBrainPwSh4KeyboardMap : public KeyboardMap {
public:
    using KeyboardMap::KeyboardMap;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SharpBrainPwSh4;
    }

    const std::vector<KeyBinding>& Bindings() const override { return kBindings; }
};

}  /* namespace */

REGISTER_SERVICE_AS(SharpBrainPwSh4KeyboardMap, KeyboardMap);

}
