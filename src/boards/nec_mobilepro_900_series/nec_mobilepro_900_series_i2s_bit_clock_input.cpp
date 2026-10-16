#include "../../socs/pxa255/pxa255_i2s_bit_clock_input.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "nec_mobilepro_900_series_family_id.h"

namespace cerfcore {

namespace {

class NecMobilepro900SeriesI2sBitClockInput : public Pxa255I2sBitClockInput {
public:
    using Pxa255I2sBitClockInput::Pxa255I2sBitClockInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::NecMobilepro900Series;
    }

    /* nec_mobilepro_900_ce4_2 dad.dll sub_1C625F8 writes WM8731 register 7 = 18 through sub_1C626DC, leaving bit
       0x40, which the Linux WM8731 driver (wm8731_set_dai_fmt) sets when the codec provides the bit clock; the Linux
       jlime MobilePro 900 board file puts the WM8731 BCLK on GPIO28. */
    GuestCycleClock::Rate PadClock() override { return GuestCycleClock::Rate{0u, 1u}; }
};

}

REGISTER_SERVICE_AS(NecMobilepro900SeriesI2sBitClockInput, Pxa255I2sBitClockInput);

}
