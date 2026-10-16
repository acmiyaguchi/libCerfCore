#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/keyboard_map.h>
#include <cerfcore/boards/board_context.h>
#include "nokia_sea_ray_family_id.h"

#include <vector>

namespace cerfcore {

namespace {

const std::vector<KeyBinding> kBindings = {
    { 0x75, 0x0000, nullptr, 0, 0 },
    { 0x76, 0x0001, nullptr, 0, 0 },
    { 0x77, 0x0002, "F20",  0, 0 },
    { 0x78, 0x0004, "F19",  0, 0 },
};

class NokiaSeaRayKeyboardMap : public KeyboardMap {
public:
    using KeyboardMap::KeyboardMap;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::NokiaSeaRay;
    }

    const std::vector<KeyBinding>& Bindings() const override {
        return kBindings;
    }
};

}

REGISTER_SERVICE_AS(NokiaSeaRayKeyboardMap, KeyboardMap);

}
