#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/keyboard_map.h>
#include "../../host/ps2_set2_keymap.h"
#include <cerfcore/boards/board_context.h>
#include "nec_sg2_vr5500_family_id.h"

#include <vector>

namespace cerfcore {

namespace {

class NecSg2Vr5500KeyboardMap : public KeyboardMap {
public:
    using KeyboardMap::KeyboardMap;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::NecSg2Vr5500;
    }

    void OnReady() override { bindings_ = Ps2Set2KeyBindings(); }

    const std::vector<KeyBinding>& Bindings() const override { return bindings_; }

private:
    std::vector<KeyBinding> bindings_;
};

}  /* namespace */

REGISTER_SERVICE_AS(NecSg2Vr5500KeyboardMap, KeyboardMap);

}
