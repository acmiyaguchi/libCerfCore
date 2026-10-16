#include "../../socs/iop13xx/iop13xx_gpio_input.h"

#include "siemens_simatic_mp377_touch_panel.h"
#include <cerfcore/boards/board_context.h>
#include "siemens_simatic_mp377_family_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include "../../socs/irq_controller.h"

namespace cerfcore {

namespace {

class SiemensSimaticMp377GpioInput final : public Iop13xxGpioInput {
public:
    using Iop13xxGpioInput::Iop13xxGpioInput;

    bool ShouldRegister() override {
        auto* board = emu_.TryGet<BoardContext>();
        return board && board->GetBoardFamilyId() == BoardFamilyId::SiemensSimaticMp377;
    }

    uint32_t ReadPins() override {
        return emu_.Get<siemens_simatic_mp377::SiemensSimaticMp377TouchPanel>().ReadPenDetectReg();
    }
};

REGISTER_SERVICE_AS(SiemensSimaticMp377GpioInput, Iop13xxGpioInput);

} // namespace

}
