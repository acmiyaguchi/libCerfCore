#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/keyboard_map.h>
#include "sharp_brain_pw_sh4_family_id.h"

#include <vector>

namespace cerfcore {

namespace {

/* device_code below 0x100 = HW_PWR_STS_PSWITCH level while held (MCIMX28RM Rev. 2 Table 11-1);
   sharp_brain_pw_sh4 keybd_EDNA2.dll KBD_ScanDebounced c08787a4 reads level 0x1 as key 0x1C.
   0x100 + row * 8 + DIN4 pin: KBD_ScanMatrix c0878678, key tables c0872d58, c087a148, c087a5ef. */
const std::vector<KeyBinding> kBindings = {
    { 0x13, 0x001, "Power", 0, 0 },
    { 0x72, 0x100, "F3", 0, 0 },
    { 0x51, 0x101, "Q", 0, 0 },
    { 0x41, 0x102, "A", 0, 0 },
    { 0x5A, 0x103, "Z", 0, 0 },
    { 0x14, 0x104, "Caps", 0, 0 },
    { 0x56, 0x106, "V", 0, 0 },
    { 0x21, 0x107, "PgUp", 0, 0 },
    { 0x70, 0x108, "F1", 0, 0 },
    { 0x57, 0x109, "W", 0, 0 },
    { 0x53, 0x10A, "S", 0, 0 },
    { 0x58, 0x10B, "X", 0, 0 },
    { 0x52, 0x10C, "R", 0, 0 },
    { 0x7A, 0x10E, "F11", 0, 0 },
    { 0x22, 0x10F, "PgDn", 0, 0 },
    { 0x71, 0x110, "F2", 0, 0 },
    { 0x45, 0x111, "E", 0, 0 },
    { 0x44, 0x112, "D", 0, 0 },
    { 0x43, 0x113, "C", 0, 0 },
    { 0x46, 0x114, "F", 0, 0 },
    { 0x20, 0x116, "Space", 0, 0 },
    { 0x78, 0x117, "F9", 0, 0 },
    { 0x73, 0x118, "F4", 0, 0 },
    { 0x54, 0x119, "T", 0, 0 },
    { 0x47, 0x11A, "G", 0, 0 },
    { 0x42, 0x11B, "B", 0, 0 },
    { 0x7B, 0x11C, "F12", 0, 0 },
    { 0x26, 0x11E, "Up", 0, 0 },
    { 0x1B, 0x11F, "Esc", 0, 0 },
    { 0x76, 0x120, "F7", 0, 0 },
    { 0x59, 0x121, "Y", 0, 0 },
    { 0x48, 0x122, "H", 0, 0 },
    { 0x4A, 0x123, "J", 0, 0 },
    { 0x4E, 0x124, "N", 0, 0 },
    { 0x27, 0x126, "Right", 0, 0 },
    { 0x25, 0x127, "Left", 0, 0 },
    { 0x77, 0x128, "F8", 0, 0 },
    { 0x55, 0x129, "U", 0, 0 },
    { 0x4F, 0x12A, "O", 0, 0 },
    { 0x4B, 0x12B, "K", 0, 0 },
    { 0x4D, 0x12C, "M", 0, 0 },
    { 0x28, 0x12E, "Down", 0, 0 },
    { 0x79, 0x130, "F10", 0, 0 },
    { 0x49, 0x131, "I", 0, 0 },
    { 0x50, 0x132, "P", 0, 0 },
    { 0x4C, 0x133, "L", 0, 0 },
    { 0xBD, 0x134, "-", 0, 0 },
    { 0x0D, 0x136, "Enter", 0, 0 },
    { 0x08, 0x137, "Backspace", 0, 0 },
};

class SharpBrainPwSh4KeyboardMap : public KeyboardMap {
public:
    using KeyboardMap::KeyboardMap;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SharpBrainPwSh4;
    }

    const std::vector<KeyBinding>& Bindings() const override { return kBindings; }
};

}  /* namespace */

REGISTER_SERVICE_AS(SharpBrainPwSh4KeyboardMap, KeyboardMap);

}
