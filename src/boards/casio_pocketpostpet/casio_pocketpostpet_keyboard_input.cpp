#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/keyboard_input.h>
#include <cerfcore/host/keyboard_map.h>
#include <cerfcore/host/keyboard_router.h>
#include "../../socs/vr41xx/vr41xx_kiu.h"
#include <cerfcore/boards/board_context.h>
#include "casio_pocketpostpet_family_id.h"
#include "casio_pocketpostpet_asic.h"

#include <cstdint>

namespace cerfcore {

namespace {

class CasioPocketpostpetKeyboardInput : public KeyboardInput {
public:
    using KeyboardInput::KeyboardInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::CasioPocketpostpet;
    }

    void OnReady() override { emu_.Get<KeyboardRouter>().Register(this); }

    void OnHostKey(uint8_t vk, bool key_up) override {
        uint32_t code;
        if (!emu_.Get<KeyboardMap>().BaseDeviceCode(vk, code))
            return;
        if (code < 0x100u)
            emu_.Get<Vr41xxKiu>().SetKeyState(static_cast<uint8_t>(code), !key_up);
        else
            emu_.Get<CasioPocketpostpetAsic>().SetSideButton(
                static_cast<uint16_t>(code & 0xFFu), !key_up);
    }
};

}  // namespace

REGISTER_SERVICE(CasioPocketpostpetKeyboardInput);

}
