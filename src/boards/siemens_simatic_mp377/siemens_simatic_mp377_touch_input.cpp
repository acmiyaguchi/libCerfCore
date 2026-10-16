#include "siemens_simatic_mp377_touch_panel.h"

#include <cerfcore/boards/board_context.h>
#include "siemens_simatic_mp377_family_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/touch_input.h>

namespace {

class SiemensSimaticMp377TouchInput : public TouchInput {
public:
    using TouchInput::TouchInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SiemensSimaticMp377;
    }

    void OnPenDown(int x, int y) override { UpdateTouch(x, y, true); }

    void OnPenMove(int x, int y) override { UpdateTouch(x, y, true); }

    void OnPenUp(int x, int y) override { UpdateTouch(x, y, false); }

    void OnCaptureLost() override {
        emu_.Get<siemens_simatic_mp377::SiemensSimaticMp377TouchPanel>().CaptureLost();
    }

private:
    void UpdateTouch(int x, int y, bool down) {
        emu_.Get<siemens_simatic_mp377::SiemensSimaticMp377TouchPanel>().UpdateHostPointer(x, y, down);
    }
};

} // namespace

REGISTER_SERVICE_AS(SiemensSimaticMp377TouchInput, TouchInput);
