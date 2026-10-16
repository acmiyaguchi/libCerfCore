#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/keyboard_map.h>
#include <cerfcore/boards/board_context.h>
#include "casio_pocketpostpet_family_id.h"

#include <vector>

namespace {

/* device_code<0x100 = KIU matrix idx (keybddr.dll sub_1362060 emits dword_1360490[idx];
   idx0=F9 6=NEXT 7=HOME 8=F11 14=END 15=PRIOR 23=F12); >=0x100 = ASIC 0x1004 side-button
   bit (keybddr sub_1361CD8 dword_13604F0: bit1=F10, bit2=0xDF poll-only). Guest-key FUNCTION
   from MCTop.exe WndProc sub_134B0 + nav neighbour table byte_151E0. */
class CasioPocketpostpetKeyboardMap : public KeyboardMap {
public:
    using KeyboardMap::KeyboardMap;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::CasioPocketpostpet;
    }

    const std::vector<KeyBinding>& Bindings() const override {
        static const std::vector<KeyBinding> kBindings = {
            {0x26u, 15u,    "Up",    0u, 0u},
            {0x28u,  6u,    "Down",  0u, 0u},
            {0x25u,  7u,    "Left",  0u, 0u},
            {0x27u, 14u,    "Right", 0u, 0u},
            {0x0Du,  0u,    "OK",    0u, 0u},
            {0x1Bu,  8u,    "Back",  0u, 0u},
            {0x08u,  8u,    "Back",  0u, 0u},
            {0x09u, 23u,    "Disp",  0u, 0u},
            {0x79u, 0x102u, "Side1", 0u, 0u},
            {0xDFu, 0x104u, "Side2", 0u, 0u},
        };
        return kBindings;
    }
};

}  // namespace

REGISTER_SERVICE_AS(CasioPocketpostpetKeyboardMap, KeyboardMap);
