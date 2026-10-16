#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/keyboard_map.h>
#include <cerfcore/boards/board_context.h>
#include "samsung_smdk2410_family_id.h"

#include <vector>

namespace cerfcore {

namespace {

/* device_code = guest_vk << 8 | scancode; inverse of the matrix scancode->VK
   table at 0x014D7140 in devemu_wm5 kbdmouse.dll, highest scancode 0x6F
   declared at 0x014D7124, layout descriptor published by Matrix 0x014D1A90
   from 0x014D712C. */
const std::vector<KeyBinding> kBindings = {
    { 0x12, 0x1201, nullptr, 0, 0 },
    { 0xC0, 0xC009, nullptr, 0, 0 },
    { 0xDC, 0xDC0A, nullptr, 0, 0 },
    { 0x09, 0x090B, nullptr, 0, 0 },
    { 0x5A, 0x5A0C, nullptr, 0, 0 },
    { 0x41, 0x410D, nullptr, 0, 0 },
    { 0x58, 0x580E, nullptr, 0, 0 },
    { 0xA0, 0xA012, nullptr, 0, 0 },
    { 0x11, 0x1119, nullptr, 0, 0 },
    { 0x1B, 0x1B29, nullptr, 0, 0 },
    { 0x2E, 0x2E2A, nullptr, 0, 0 },
    { 0x51, 0x512B, nullptr, 0, 0 },
    { 0x14, 0x142C, nullptr, 0, 0 },
    { 0x53, 0x532D, nullptr, 0, 0 },
    { 0x43, 0x432E, nullptr, 0, 0 },
    { 0x33, 0x332F, nullptr, 0, 0 },
    { 0x31, 0x3131, nullptr, 0, 0 },
    { 0x57, 0x5733, nullptr, 0, 0 },
    { 0x44, 0x4435, nullptr, 0, 0 },
    { 0x56, 0x5636, nullptr, 0, 0 },
    { 0x34, 0x3437, nullptr, 0, 0 },
    { 0x32, 0x3239, nullptr, 0, 0 },
    { 0x54, 0x543A, nullptr, 0, 0 },
    { 0x45, 0x453B, nullptr, 0, 0 },
    { 0x46, 0x463D, nullptr, 0, 0 },
    { 0x42, 0x423E, nullptr, 0, 0 },
    { 0x35, 0x353F, nullptr, 0, 0 },
    { 0x39, 0x3941, nullptr, 0, 0 },
    { 0x59, 0x5942, nullptr, 0, 0 },
    { 0x52, 0x5243, nullptr, 0, 0 },
    { 0x4B, 0x4B44, nullptr, 0, 0 },
    { 0x47, 0x4745, nullptr, 0, 0 },
    { 0x4E, 0x4E46, nullptr, 0, 0 },
    { 0x36, 0x3647, nullptr, 0, 0 },
    { 0x30, 0x3049, nullptr, 0, 0 },
    { 0x55, 0x554A, nullptr, 0, 0 },
    { 0x4F, 0x4F4B, nullptr, 0, 0 },
    { 0x4C, 0x4C4C, nullptr, 0, 0 },
    { 0x48, 0x484D, nullptr, 0, 0 },
    { 0x4D, 0x4D4E, nullptr, 0, 0 },
    { 0x37, 0x374F, nullptr, 0, 0 },
    { 0xBD, 0xBD51, nullptr, 0, 0 },
    { 0x49, 0x4952, nullptr, 0, 0 },
    { 0x50, 0x5053, nullptr, 0, 0 },
    { 0xBA, 0xBA54, nullptr, 0, 0 },
    { 0x4A, 0x4A55, nullptr, 0, 0 },
    { 0xBC, 0xBC56, nullptr, 0, 0 },
    { 0x38, 0x3857, nullptr, 0, 0 },
    { 0xBB, 0xBB59, nullptr, 0, 0 },
    { 0x0D, 0x0D5A, nullptr, 0, 0 },
    { 0xDB, 0xDB5B, nullptr, 0, 0 },
    { 0xDE, 0xDE5C, nullptr, 0, 0 },
    { 0xBF, 0xBF5D, nullptr, 0, 0 },
    { 0xBE, 0xBE5E, nullptr, 0, 0 },
    { 0x5C, 0x5C5F, nullptr, 0, 0 },
    { 0x5B, 0x5B60, nullptr, 0, 0 },
    { 0xA1, 0xA162, nullptr, 0, 0 },
    { 0x08, 0x0869, nullptr, 0, 0 },
    { 0x28, 0x286A, nullptr, 0, 0 },
    { 0xDD, 0xDD6B, nullptr, 0, 0 },
    { 0x26, 0x266C, nullptr, 0, 0 },
    { 0x25, 0x256D, nullptr, 0, 0 },
    { 0x20, 0x206E, nullptr, 0, 0 },
    { 0x27, 0x276F, nullptr, 0, 0 },
    { 0x10, 0xA012, nullptr, 0, 0 },
    { 0xA2, 0x1119, nullptr, 0, 0 },
    { 0xA3, 0x1119, nullptr, 0, 0 },
    { 0xA4, 0x1201, nullptr, 0, 0 },
    { 0xA5, 0x1201, nullptr, 0, 0 },
    { 0x70, 0xC265, "LSK",  0, 0 },
    { 0x71, 0xC366, "RSK",  0, 0 },
    { 0x72, 0xC467, "Dial", 0, 0 },
    { 0x73, 0xC568, "Hang", 0, 0 },
    { 0x74, 0xC121, "Fn",   0, 0 },
};

class SamsungSmdk2410KeyboardMap : public KeyboardMap {
public:
    using KeyboardMap::KeyboardMap;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SamsungSmdk2410;
    }

    const std::vector<KeyBinding>& Bindings() const override { return kBindings; }
};

}  /* namespace */

REGISTER_SERVICE_AS(SamsungSmdk2410KeyboardMap, KeyboardMap);

}
