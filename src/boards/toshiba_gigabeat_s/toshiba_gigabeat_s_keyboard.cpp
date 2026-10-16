#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "toshiba_gigabeat_s_family_id.h"
#include <cerfcore/host/keyboard_input.h>
#include <cerfcore/host/keyboard_map.h>
#include <cerfcore/host/keyboard_router.h>
#include "../../socs/imx31/imx31_kpp.h"

#include <cstdint>

namespace {

class ToshibaGigabeatSKeyboardInput : public KeyboardInput {
public:
    using KeyboardInput::KeyboardInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::ToshibaGigabeatS;
    }

    void OnReady() override { emu_.Get<KeyboardRouter>().Register(this); }

    void OnHostKey(uint8_t vk, bool key_up) override {
        uint32_t code;
        if (!emu_.Get<KeyboardMap>().BaseDeviceCode(vk, code)) return;
        const uint8_t col = static_cast<uint8_t>(code >> 8);
        const uint8_t row = static_cast<uint8_t>(code & 0xFFu);
        emu_.Get<Imx31Kpp>().SetMatrixKey(col, row, !key_up);
    }
};

}  /* namespace */

REGISTER_SERVICE(ToshibaGigabeatSKeyboardInput);
