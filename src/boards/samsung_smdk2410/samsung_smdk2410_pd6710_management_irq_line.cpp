#include "../../peripherals/cirrus_pd6710/pd6710_management_irq_line.h"

#include <cerfcore/boards/board_context.h>
#include "samsung_smdk2410_family_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include "../../socs/irq_controller.h"

namespace {

constexpr int kEint3SourceBit = 3;

class SamsungSmdk2410Pd6710ManagementIrqLine : public Pd6710ManagementIrqLine {
public:
    using Pd6710ManagementIrqLine::Pd6710ManagementIrqLine;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SamsungSmdk2410;
    }

    void Pulse() override {
        emu_.Get<IrqController>().AssertIrq(kEint3SourceBit);
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(SamsungSmdk2410Pd6710ManagementIrqLine, Pd6710ManagementIrqLine);
