#include "../../peripherals/intel_sa1111/sa1111_gpio_port_a_sink.h"

#include <cerfcore/boards/board_context.h>
#include "hp_jornada_700_family_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include "../../peripherals/pcmcia/pcmcia_slot.h"
#include "../../peripherals/pcmcia/pcmcia_space_router.h"

namespace {

/* Jornada 720 PCMCIA/CF power controller on SA-1111 GPIO_A (hardware
   doc jornada720.txt §3.2: PC Card power on bits 0/1 - the ROM driver
   drives bit 0, the doc names bit 1; either rail means Vcc applied -
   bit 2 selects 3 V/5 V, CF power on bit 3). */
constexpr uint8_t kPcCardPowerBits = 0x03u;
constexpr uint8_t kCfPowerBit      = 0x08u;

class HpJornada700PcmciaPower : public Sa1111GpioPortASink {
public:
    using Sa1111GpioPortASink::Sa1111GpioPortASink;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::HpJornada700;
    }

    void OnPortAOutputs(uint8_t levels) override {
        auto& space = emu_.Get<PcmciaSpaceRouter>();
        if (auto* s0 = space.Socket(0)) {
            s0->SetPowered((levels & kPcCardPowerBits) != 0u);
        }
        if (auto* s1 = space.Socket(1)) {
            s1->SetPowered((levels & kCfPowerBit) != 0u);
        }
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(HpJornada700PcmciaPower, Sa1111GpioPortASink);
