#include <cerfcore/core/cerf_emulator.h>
#include "../../peripherals/qualcomm_pm8058/pm8058_irq_line.h"
#include "../../socs/msm8255/msm8255_gpio_bus.h"
#include <cerfcore/boards/board_context.h>
#include "nokia_sea_ray_family_id.h"

#include <cstdint>

namespace cerfcore {

namespace {

constexpr uint32_t kPmicIrqGpio = 27u;

class NokiaSeaRayPm8058IrqLine : public Pm8058IrqLine {
public:
    using Pm8058IrqLine::Pm8058IrqLine;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::NokiaSeaRay;
    }

    void OnReady() override { SetPm8058IrqAsserted(false); }

    /* Linux drivers/mfd/pm8058-core.c requests the part's parent interrupt as
       IRQ_TYPE_LEVEL_LOW, so an asserted output pulls the pin down. */
    void SetPm8058IrqAsserted(bool asserted) override {
        emu_.Get<Msm8255GpioBus>().SetInputPin(kPmicIrqGpio, !asserted);
    }
};

}

REGISTER_SERVICE_AS(NokiaSeaRayPm8058IrqLine, Pm8058IrqLine);

}
