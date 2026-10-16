#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/relative_mouse_input.h>
#include "../../peripherals/intel_i8042/i8042_controller.h"
#include <cerfcore/boards/board_context.h>
#include "nec_sg2_vr5500_family_id.h"

namespace {

/* kRelMouseLeft/Right (0x1/0x2) match Ps2Mouse::kButtonLeft/Right, so the button
   mask passes through unchanged. */
class NecSg2Vr5500Mouse : public RelativeMouseInput {
public:
    using RelativeMouseInput::RelativeMouseInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::NecSg2Vr5500;
    }

    void OnRelativeMove(int dx, int dy, uint32_t button_mask) override {
        emu_.Get<I8042Controller>().HostMouseMotion(dx, dy, button_mask);
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(NecSg2Vr5500Mouse, RelativeMouseInput);
