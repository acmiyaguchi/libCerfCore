#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/keyboard_map.h>
#include <cerfcore/boards/board_context.h>
#include "casio_tomcat_family_id.h"

#include <vector>

namespace cerfcore {

namespace {

/* device_code is the GIU pin. casio_cassiopeia_e55 keybddr.dll sub_14E37C4 @0x14E37C4 forms
   scan lane N from GIUPIODL bit N+5, and lanes 9-12 from GIUPIODH D13/D15/D14 and GIUPIODL
   D14; sub_14E25A0 @0x14E25A0 maps lane N to the guest VK at 0x14E0C27 + N + 1. */
const std::vector<KeyBinding> kBindings = {
    { 0x0D,  6, "Action", 0, 0 },
    { 0x1B,  7, "Cancel", 0, 0 },
    { 0x70,  8, "App1",   0, 0 },
    { 0x71,  9, "App2",   0, 0 },
    { 0x72, 10, "App3",   0, 0 },
    { 0x73, 11, "App4",   0, 0 },
    { 0x22, 12, "Down",   0, 0 },
    { 0x21, 13, "Up",     0, 0 },
    { 0x26, 29, nullptr,   0, 0 },
    { 0x28, 14, nullptr,   0, 0 },
    { 0x25, 31, nullptr,   0, 0 },
    { 0x27, 30, nullptr,   0, 0 },
};

class CasioTomcatKeyboardMap : public KeyboardMap {
public:
    using KeyboardMap::KeyboardMap;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::CasioTomcat;
    }

    const std::vector<KeyBinding>& Bindings() const override { return kBindings; }
};

}

REGISTER_SERVICE_AS(CasioTomcatKeyboardMap, KeyboardMap);

}
