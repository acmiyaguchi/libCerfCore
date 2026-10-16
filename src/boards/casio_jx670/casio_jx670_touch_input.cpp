#include <cerfcore/boards/board_context.h>
#include "casio_jx670_family_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/touch_input.h>
#include "casio_jx670_companion.h"

namespace {

class CasioJx670TouchInput : public TouchInput {
public:
    using TouchInput::TouchInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::CasioJx670;
    }

    void OnPenDown(int x, int y) override {
        emu_.Get<CasioJx670Companion>().SetTouchPen(true, x, y);
    }
    void OnPenMove(int x, int y) override {
        emu_.Get<CasioJx670Companion>().SetTouchPen(true, x, y);
    }
    void OnPenUp(int x, int y) override {
        emu_.Get<CasioJx670Companion>().SetTouchPen(false, x, y);
    }
    void OnCaptureLost() override {
        emu_.Get<CasioJx670Companion>().TouchCaptureLost();
    }
};

}

REGISTER_SERVICE_AS(CasioJx670TouchInput, TouchInput);
