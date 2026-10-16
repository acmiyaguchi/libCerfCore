#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/keyboard_map.h>
#include <cerfcore/boards/board_context.h>
#include "philips_velo_1_family_id.h"

#include <vector>

namespace {

/* device_code = scancode; inverse of keybddr sub_1F3C830's scancode->VK table
   (nk.bin @ 0x1D8F88). */
class PhilipsVelo1KeyboardMap : public KeyboardMap {
public:
    using KeyboardMap::KeyboardMap;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::PhilipsVelo1;
    }

    const std::vector<KeyBinding>& Bindings() const override {
        static const std::vector<KeyBinding> kBindings = {
            {0x41u, 0x14u, nullptr, 0u, 0u},  {0x42u, 0x2Bu, nullptr, 0u, 0u},
            {0x43u, 0x2Au, nullptr, 0u, 0u},  {0x44u, 0x2Cu, nullptr, 0u, 0u},
            {0x45u, 0x28u, nullptr, 0u, 0u},  {0x46u, 0x34u, nullptr, 0u, 0u},
            {0x47u, 0x38u, nullptr, 0u, 0u},  {0x48u, 0x40u, nullptr, 0u, 0u},
            {0x49u, 0x45u, nullptr, 0u, 0u},  {0x4Au, 0x3Cu, nullptr, 0u, 0u},
            {0x4Bu, 0x44u, nullptr, 0u, 0u},  {0x4Cu, 0x36u, nullptr, 0u, 0u},
            {0x4Du, 0x3Bu, nullptr, 0u, 0u},  {0x4Eu, 0x33u, nullptr, 0u, 0u},
            {0x4Fu, 0x3Eu, nullptr, 0u, 0u},  {0x50u, 0x4Du, nullptr, 0u, 0u},
            {0x51u, 0x26u, nullptr, 0u, 0u},  {0x52u, 0x30u, nullptr, 0u, 0u},
            {0x53u, 0x24u, nullptr, 0u, 0u},  {0x54u, 0x2Du, nullptr, 0u, 0u},
            {0x55u, 0x3Du, nullptr, 0u, 0u},  {0x56u, 0x23u, nullptr, 0u, 0u},
            {0x57u, 0x18u, nullptr, 0u, 0u},  {0x58u, 0x22u, nullptr, 0u, 0u},
            {0x59u, 0x35u, nullptr, 0u, 0u},  {0x5Au, 0x12u, nullptr, 0u, 0u},
            {0x30u, 0x47u, nullptr, 0u, 0u},  {0x31u, 0x13u, nullptr, 0u, 0u},
            {0x32u, 0x16u, nullptr, 0u, 0u},  {0x33u, 0x15u, nullptr, 0u, 0u},
            {0x34u, 0x25u, nullptr, 0u, 0u},  {0x35u, 0x17u, nullptr, 0u, 0u},
            {0x36u, 0x27u, nullptr, 0u, 0u},  {0x37u, 0x2Fu, nullptr, 0u, 0u},
            {0x38u, 0x37u, nullptr, 0u, 0u},  {0x39u, 0x3Fu, nullptr, 0u, 0u},
            {0x20u, 0x21u, nullptr, 0u, 0u},  {0x09u, 0x11u, nullptr, 0u, 0u},
            {0x08u, 0x39u, nullptr, 0u, 0u},  {0x0Du, 0x4Bu, nullptr, 0u, 0u},
            {0x1Bu, 0x29u, nullptr, 0u, 0u},  {0x10u, 0x51u, nullptr, 0u, 0u},
            {0x11u, 0x01u, nullptr, 0u, 0u},  {0x12u, 0x19u, nullptr, 0u, 0u},
            {0x5Bu, 0x09u, nullptr, 0u, 0u},  {0x25u, 0x41u, nullptr, 0u, 0u},
            {0x26u, 0x4Au, nullptr, 0u, 0u},  {0x27u, 0x32u, nullptr, 0u, 0u},
            {0x28u, 0x49u, nullptr, 0u, 0u},  {0xBAu, 0x4Cu, nullptr, 0u, 0u},
            {0xBBu, 0x4Fu, nullptr, 0u, 0u},  {0xBCu, 0x43u, nullptr, 0u, 0u},
            {0xBDu, 0x4Eu, nullptr, 0u, 0u},  {0xBEu, 0x3Au, nullptr, 0u, 0u},
            {0xBFu, 0x42u, nullptr, 0u, 0u},  {0xC0u, 0x31u, nullptr, 0u, 0u},
            {0xDBu, 0x48u, nullptr, 0u, 0u},  {0xDCu, 0x50u, nullptr, 0u, 0u},
            {0xDDu, 0x46u, nullptr, 0u, 0u},  {0xDEu, 0x2Eu, nullptr, 0u, 0u},
            {0xDFu, 0x53u, nullptr, 0u, 0u},
        };
        return kBindings;
    }
};

}  // namespace

REGISTER_SERVICE_AS(PhilipsVelo1KeyboardMap, KeyboardMap);
