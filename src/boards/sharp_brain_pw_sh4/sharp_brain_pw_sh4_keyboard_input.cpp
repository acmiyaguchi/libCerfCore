#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/keyboard_input.h>
#include <cerfcore/host/keyboard_map.h>
#include <cerfcore/host/keyboard_router.h>
#include "../../socs/imx28/imx28_power.h"
#include "sharp_brain_pw_sh4_family_id.h"

#include <cstdint>
#include <string>

namespace cerfcore {

namespace {

class SharpBrainPwSh4KeyboardInput : public KeyboardInput {
public:
    using KeyboardInput::KeyboardInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SharpBrainPwSh4;
    }

    void OnReady() override { emu_.Get<KeyboardRouter>().Register(this); }

    std::string SourceName() const override { return "PW-SH4 keyboard"; }

    void OnHostKey(uint8_t vk, bool key_up) override {
        uint32_t level;
        if (!emu_.Get<KeyboardMap>().BaseDeviceCode(vk, level)) return;
        emu_.Get<Imx28Power>().SetPswitchLevel(key_up ? 0u : level);
    }
};

}  /* namespace */

REGISTER_SERVICE(SharpBrainPwSh4KeyboardInput);

}
