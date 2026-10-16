#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/keyboard_map.h>
#include <cerfcore/boards/board_context.h>
#include "nec_mobilepro_700_family_id.h"

#include <vector>

namespace cerfcore {

namespace {

/* device_code = KIU matrix index (16*KIUDATn + bit), reversed from keybddr's
   scan-index -> VKey table (sub_15B4848 @ 0x15B0978). L/R modifiers fold to
   VK_SHIFT/CONTROL/MENU (host_input_capture), so they bind the left cells. */
class NecMobilePro700KeyboardMap : public KeyboardMap {
public:
    using KeyboardMap::KeyboardMap;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::NecMobilepro700;
    }

    const std::vector<KeyBinding>& Bindings() const override {
        static const std::vector<KeyBinding> kBindings = {
            {0x27u, 0u, nullptr, 0u, 0u},
            {0xDCu, 1u, nullptr, 0u, 0u},
            {0x50u, 2u, nullptr, 0u, 0u},
            {0xBEu, 3u, nullptr, 0u, 0u},
            {0x59u, 4u, nullptr, 0u, 0u},
            {0x42u, 5u, nullptr, 0u, 0u},
            {0x5Au, 6u, nullptr, 0u, 0u},
            {0x20u, 7u, nullptr, 0u, 0u},
            {0x28u, 8u, nullptr, 0u, 0u},
            {0xBFu, 9u, nullptr, 0u, 0u},
            {0x4Fu, 10u, nullptr, 0u, 0u},
            {0xBCu, 11u, nullptr, 0u, 0u},
            {0x54u, 12u, nullptr, 0u, 0u},
            {0x56u, 13u, nullptr, 0u, 0u},
            {0x41u, 14u, nullptr, 0u, 0u},
            {0x25u, 16u, nullptr, 0u, 0u},
            {0x0Du, 17u, nullptr, 0u, 0u},
            {0x49u, 18u, nullptr, 0u, 0u},
            {0x4Du, 19u, nullptr, 0u, 0u},
            {0x52u, 20u, nullptr, 0u, 0u},
            {0x43u, 21u, nullptr, 0u, 0u},
            {0x57u, 22u, nullptr, 0u, 0u},
            {0x5Bu, 23u, nullptr, 0u, 0u},
            {0x90u, 24u, nullptr, 0u, 0u},
            {0xDDu, 25u, nullptr, 0u, 0u},
            {0x55u, 26u, nullptr, 0u, 0u},
            {0x4Eu, 27u, nullptr, 0u, 0u},
            {0x45u, 28u, nullptr, 0u, 0u},
            {0x58u, 29u, nullptr, 0u, 0u},
            {0x51u, 30u, nullptr, 0u, 0u},
            {0x14u, 31u, nullptr, 0u, 0u},
            {0x30u, 34u, nullptr, 0u, 0u},
            {0x4Cu, 35u, nullptr, 0u, 0u},
            {0x36u, 36u, nullptr, 0u, 0u},
            {0x47u, 37u, nullptr, 0u, 0u},
            {0x09u, 38u, nullptr, 0u, 0u},
            {0x1Bu, 39u, nullptr, 0u, 0u},
            {0xBAu, 41u, nullptr, 0u, 0u},
            {0x39u, 42u, nullptr, 0u, 0u},
            {0x4Bu, 43u, nullptr, 0u, 0u},
            {0x35u, 44u, nullptr, 0u, 0u},
            {0x46u, 45u, nullptr, 0u, 0u},
            {0x32u, 46u, nullptr, 0u, 0u},
            {0xC0u, 47u, nullptr, 0u, 0u},
            {0x26u, 48u, nullptr, 0u, 0u},
            {0xDBu, 49u, nullptr, 0u, 0u},
            {0x38u, 50u, nullptr, 0u, 0u},
            {0x4Au, 51u, nullptr, 0u, 0u},
            {0x34u, 52u, nullptr, 0u, 0u},
            {0x44u, 53u, nullptr, 0u, 0u},
            {0x31u, 54u, nullptr, 0u, 0u},
            {0xDEu, 55u, nullptr, 0u, 0u},
            {0xBDu, 56u, nullptr, 0u, 0u},
            {0x37u, 58u, nullptr, 0u, 0u},
            {0x48u, 59u, nullptr, 0u, 0u},
            {0x33u, 60u, nullptr, 0u, 0u},
            {0x53u, 61u, nullptr, 0u, 0u},
            {0x2Eu, 62u, nullptr, 0u, 0u},
            {0x08u, 68u, nullptr, 0u, 0u},
            {0x7Bu, 69u, nullptr, 0u, 0u},
            {0x77u, 70u, nullptr, 0u, 0u},
            {0x73u, 71u, nullptr, 0u, 0u},
            {0x12u, 73u, nullptr, 0u, 0u},
            {0x7Au, 77u, nullptr, 0u, 0u},
            {0x76u, 78u, nullptr, 0u, 0u},
            {0x72u, 79u, nullptr, 0u, 0u},
            {0x11u, 82u, nullptr, 0u, 0u},
            {0x79u, 85u, nullptr, 0u, 0u},
            {0x75u, 86u, nullptr, 0u, 0u},
            {0x71u, 87u, nullptr, 0u, 0u},
            {0x10u, 91u, nullptr, 0u, 0u},
            {0xBBu, 92u, nullptr, 0u, 0u},
            {0x78u, 93u, nullptr, 0u, 0u},
            {0x74u, 94u, nullptr, 0u, 0u},
            {0x70u, 95u, nullptr, 0u, 0u},
        };
        return kBindings;
    }
};

}  // namespace

REGISTER_SERVICE_AS(NecMobilePro700KeyboardMap, KeyboardMap);

}
