#include "hp_jornada_700_keyboard.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/keyboard_input.h>
#include <cerfcore/host/keyboard_map.h>
#include <cerfcore/host/keyboard_router.h>
#include "../../socs/sa11xx/sa11xx_gpio.h"
#include <cerfcore/boards/board_context.h>
#include "hp_jornada_700_family_id.h"

namespace {

class HpJornada700KeyboardInput : public KeyboardInput {
public:
    using KeyboardInput::KeyboardInput;
    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::HpJornada700;
    }
    void OnReady() override { emu_.Get<KeyboardRouter>().Register(this); }
    void OnHostKey(uint8_t vk, bool key_up) override {
        emu_.Get<HpJornada700Keyboard>().OnHostKey(vk, key_up);
    }
};

}  /* namespace */

REGISTER_SERVICE(HpJornada700Keyboard);
REGISTER_SERVICE(HpJornada700KeyboardInput);

bool HpJornada700Keyboard::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::HpJornada700;
}

void HpJornada700Keyboard::OnReady() {
    /* GPIO0 idles high; a key pulls it low (falling edge). Publish idle-high so
       the first key produces a clean falling edge (HP doc §4.3). */
    emu_.Get<Sa11xxGpio>().DriveInputPin(0, /*level=*/true);
}

void HpJornada700Keyboard::OnHostKey(uint8_t vk, bool key_up) {
    uint32_t code;
    if (!emu_.Get<KeyboardMap>().BaseDeviceCode(vk, code)) return;
    const uint8_t sc = static_cast<uint8_t>(code);
    {
        std::lock_guard<std::mutex> lk(mtx_);
        pending_.push_back(key_up ? static_cast<uint8_t>(sc | 0x80) : sc);
    }
    PulseKbdIrqLine();
}

void HpJornada700Keyboard::DrainScancodes(std::vector<uint8_t>& out) {
    std::lock_guard<std::mutex> lk(mtx_);
    out.insert(out.end(), pending_.begin(), pending_.end());
    pending_.clear();
}

void HpJornada700Keyboard::PulseKbdIrqLine() {
    /* Drive the high->low edge that latches Sa11xxGpio GEDR bit0 -> INTC source
       0 (HP doc §4.3 falling-edge), then return to idle high for the next key. */
    auto& gpio = emu_.Get<Sa11xxGpio>();
    gpio.DriveInputPin(0, false);
    gpio.DriveInputPin(0, true);
}
