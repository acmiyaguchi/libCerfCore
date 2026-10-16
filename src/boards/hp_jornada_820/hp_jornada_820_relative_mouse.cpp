#include <cerfcore/host/relative_mouse_input.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "hp_jornada_820_family_id.h"
#include "hp_jornada_820_companion_asic.h"

namespace {

class HpJornada820RelativeMouse : public RelativeMouseInput {
public:
    using RelativeMouseInput::RelativeMouseInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::HpJornada820;
    }

    void OnRelativeMove(int dx, int dy, uint32_t button_mask) override {
        emu_.Get<HpJornada820CompanionAsic>().QueuePs2Motion(dx, dy, button_mask);
    }

    std::string SourceName() const override { return "Stock PS/2 mouse"; }
};

}  /* namespace */

REGISTER_SERVICE_AS(HpJornada820RelativeMouse, RelativeMouseInput);
