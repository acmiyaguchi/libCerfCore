#include "siemens_simpad_keypad.h"

#include <cerfcore/host/keyboard_input.h>
#include <cerfcore/host/keyboard_map.h>
#include <cerfcore/host/keyboard_router.h>
#include <cerfcore/boards/board_context.h>
#include "siemens_simpad_family_id.h"
#include <cerfcore/core/cerf_emulator.h>

#include <array>

namespace cerfcore {

bool SiemensSimpadKeypad::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::SiemensSimpad;
}

REGISTER_SERVICE(SiemensSimpadKeypad);

namespace {

class SiemensSimpadKeyboardInput : public KeyboardInput {
public:
    using KeyboardInput::KeyboardInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SiemensSimpad;
    }

    void OnReady() override { emu_.Get<KeyboardRouter>().Register(this); }

    void OnHostKey(uint8_t vk, bool key_up) override {
        auto& map = emu_.Get<KeyboardMap>();
        uint32_t code;
        if (!map.BaseDeviceCode(vk, code)) return;
        down_[vk] = !key_up;
        uint8_t mask = 0;
        for (int v = 0; v < 256; ++v) {
            uint32_t btn;
            if (down_[v] && map.BaseDeviceCode(static_cast<uint8_t>(v), btn))
                mask |= static_cast<uint8_t>(1u << btn);
        }
        emu_.Get<SiemensSimpadKeypad>().SetPressedMask(mask);
    }

private:
    std::array<bool, 256> down_{};
};

}  /* namespace */

REGISTER_SERVICE(SiemensSimpadKeyboardInput);

}
