#include "../../peripherals/cirrus_pd6710/pd6710_card_irq_line.h"

#include <cerfcore/boards/board_context.h>
#include "samsung_smdk2410_family_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include "../../socs/s3c2410/s3c2410_eint_source.h"

namespace cerfcore {

namespace {

constexpr int kPd6710CardEintNumber = 8;

class SamsungSmdk2410Pd6710CardIrqLine : public Pd6710CardIrqLine {
public:
    using Pd6710CardIrqLine::Pd6710CardIrqLine;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SamsungSmdk2410;
    }

    /* CL-PD6710/'22 data sheet v3.1 p. 58 §9.1 Misc Control 1 (index 16h)
       bit 3 "Pulse System IRQ" RW:0, p. 59: that value passes RDY/-IREQ
       interrupts to IRQ[XX] level-sensitive. */
    void Assert() override {
        emu_.Get<S3C2410EintSource>().DriveEintPin(kPd6710CardEintNumber, true);
    }

    void Deassert() override {
        emu_.Get<S3C2410EintSource>().DriveEintPin(kPd6710CardEintNumber, false);
    }
};

}

REGISTER_SERVICE_AS(SamsungSmdk2410Pd6710CardIrqLine, Pd6710CardIrqLine);

}
