#include <cerfcore/host/touch_input.h>

#include <cerfcore/boards/board_context.h>
#include "odo_family_id.h"
#include <cerfcore/core/cerf_emulator.h>

#include "odo_touch_sound.h"

namespace {

class OdoTouchInput : public TouchInput {
public:
    using TouchInput::TouchInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::Odo;
    }

    void OnPenDown    (int x, int y) override {
        emu_.Get<OdoTouchSound>().OnPenDown(x, y);
    }
    void OnPenMove    (int x, int y) override {
        emu_.Get<OdoTouchSound>().OnPenMove(x, y);
    }
    void OnPenUp      (int x, int y) override {
        (void)x; (void)y;
        emu_.Get<OdoTouchSound>().OnPenUp();
    }
    void OnCaptureLost() override {
        emu_.Get<OdoTouchSound>().OnPenUp();
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(OdoTouchInput, TouchInput);
