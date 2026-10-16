#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/keyboard_map.h>
#include <cerfcore/boards/board_context.h>
#include "siemens_simpad_family_id.h"

#include <vector>

namespace {

/* device_code = SIMpad keypad button 0..5. Each button's guest action is the
   device's observed nav behavior: 0=F1, 1=Alt, 2=Up, 3=Down, 4=Esc(Back),
   5=Enter(OK). Host arrows land on the arrow buttons directly (identity); host
   PgUp/PgDn are bonus keys routed to the same Up/Down buttons. */
const std::vector<KeyBinding> kBindings = {
    { 0x70, 0, nullptr,  0, 0 },   /* F1     -> PROG1 (guest F1)        */
    { 0x12, 1, nullptr,  0, 0 },   /* Alt    -> PROG2 (guest Alt)       */
    { 0x71, 1, "Alt",   0, 0 },
    { 0x26, 2, nullptr,  0, 0 },   /* Up     -> UP    (guest Up)        */
    { 0x21, 2, "↑",     0, 0 },
    { 0x28, 3, nullptr,  0, 0 },   /* Down   -> DOWN  (guest Down)      */
    { 0x22, 3, "↓",     0, 0 },
    { 0x25, 4, "Esc",   0, 0 },
    { 0x1B, 4, nullptr,  0, 0 },   /* Esc    -> LEFT  (guest Esc/Back)  */
    { 0x27, 5, "Enter", 0, 0 },
    { 0x0D, 5, nullptr,  0, 0 },   /* Enter  -> RIGHT (guest Enter/OK)  */
};

class SiemensSimpadKeyboardMap : public KeyboardMap {
public:
    using KeyboardMap::KeyboardMap;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SiemensSimpad;
    }

    const std::vector<KeyBinding>& Bindings() const override { return kBindings; }
};

}  /* namespace */

REGISTER_SERVICE_AS(SiemensSimpadKeyboardMap, KeyboardMap);
