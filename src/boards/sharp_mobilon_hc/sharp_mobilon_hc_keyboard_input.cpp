#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/keyboard_input.h>
#include <cerfcore/host/keyboard_map.h>
#include <cerfcore/host/keyboard_router.h>
#include "sharp_mobilon_hc_key_matrix.h"
#include <cerfcore/boards/board_context.h>
#include "sharp_mobilon_hc_family_id.h"

#include <cstdint>
#include <string>

namespace {

class SharpMobilonHcKeyboardInput : public KeyboardInput {
public:
    using KeyboardInput::KeyboardInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SharpMobilonHc;
    }

    void OnReady() override { emu_.Get<KeyboardRouter>().Register(this); }

    std::string SourceName() const override { return "HC-4100 keyboard"; }

    void OnHostKey(uint8_t vk, bool key_up) override {
        uint32_t code;
        if (!emu_.Get<KeyboardMap>().BaseDeviceCode(vk, code)) return;
        emu_.Get<SharpMobilonHcKeyMatrix>().SetKey(
            static_cast<uint8_t>(code >> 4), static_cast<uint8_t>(code & 0xF), !key_up);
    }
};

}  /* namespace */

REGISTER_SERVICE(SharpMobilonHcKeyboardInput);
