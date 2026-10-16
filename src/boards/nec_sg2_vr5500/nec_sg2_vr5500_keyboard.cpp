#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/keyboard_input.h>
#include <cerfcore/host/keyboard_map.h>
#include <cerfcore/host/keyboard_router.h>
#include "../../peripherals/intel_i8042/i8042_controller.h"
#include <cerfcore/boards/board_context.h>
#include "nec_sg2_vr5500_family_id.h"

#include <cstddef>
#include <cstdint>

namespace {

constexpr uint8_t kPs2ExtendedPrefix = 0xE0u;
constexpr uint8_t kPs2KeyUpPrefix    = 0xF0u;

class NecSg2Vr5500KeyboardInput : public KeyboardInput {
public:
    using KeyboardInput::KeyboardInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::NecSg2Vr5500;
    }

    void OnReady() override { emu_.Get<KeyboardRouter>().Register(this); }

    void OnHostKey(uint8_t vk, bool key_up) override {
        uint32_t code;
        if (!emu_.Get<KeyboardMap>().BaseDeviceCode(vk, code)) return;
        const uint8_t sc       = static_cast<uint8_t>(code & 0xFFu);
        const bool    extended = (code & 0x100u) != 0;

        uint8_t bytes[3];
        size_t  n = 0;
        if (extended) bytes[n++] = kPs2ExtendedPrefix;
        if (key_up)   bytes[n++] = kPs2KeyUpPrefix;
        bytes[n++] = sc;
        emu_.Get<I8042Controller>().HostKeyboardScancodes(bytes, n);
    }
};

}  /* namespace */

REGISTER_SERVICE(NecSg2Vr5500KeyboardInput);
