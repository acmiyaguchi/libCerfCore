#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/keyboard_map.h>
#include <cerfcore/boards/board_context.h>
#include "toshiba_gigabeat_s_family_id.h"

#include <vector>

namespace cerfcore {

namespace {

/* device_code = i.MX31 KPP cell, col<<8 | row. pyxis_keybd.dll's cell->VK table
   is cell0..3 = LEFT/UP/DOWN/RIGHT, cell5 = VK_BACK, cell6 = VK_RETURN (OK),
   cell15 = VK_MEDIA_PLAY_PAUSE (cell = 5*col + row). guest_label set only where
   the host key differs from the guest action. */
const std::vector<KeyBinding> kBindings = {
    { 0x25, 0x0000, nullptr,        0, 0 },   /* Left  -> cell0  LEFT  */
    { 0x26, 0x0001, nullptr,        0, 0 },   /* Up    -> cell1  UP    */
    { 0x28, 0x0002, nullptr,        0, 0 },   /* Down  -> cell2  DOWN  */
    { 0x27, 0x0003, nullptr,        0, 0 },   /* Right -> cell3  RIGHT */
    { 0x0D, 0x0101, nullptr,        0, 0 },   /* Enter -> cell6  OK    */
    { 0x1B, 0x0100, "Back",        0, 0 },
    { 0x08, 0x0100, nullptr,        0, 0 },   /* Bksp  -> cell5  Back  */
    { 0x20, 0x0300, "Play/Pause",  0, 0 },
    { 0xB3, 0x0300, nullptr,        0, 0 },   /* MediaPlayPause -> cell15 */
};

class ToshibaGigabeatSKeyboardMap : public KeyboardMap {
public:
    using KeyboardMap::KeyboardMap;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::ToshibaGigabeatS;
    }

    const std::vector<KeyBinding>& Bindings() const override { return kBindings; }
};

}  /* namespace */

REGISTER_SERVICE_AS(ToshibaGigabeatSKeyboardMap, KeyboardMap);

}
