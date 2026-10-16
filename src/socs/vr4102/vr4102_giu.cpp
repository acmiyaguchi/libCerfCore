#include "../vr41xx/vr41xx_giu_impl.h"

#include <cstdint>
#include "vr4102_id.h"

namespace {

using cerf_vr41xx_giu_detail::Vr41xxGiuBase;
using cerf_vr41xx_giu_detail::Vr41xxGiuModel;

/* VR4102 GIU (UM Table 18-2): GIUIOSELL..GIUPODATH at 0x0B000100-0x0B00011E. */
constexpr Vr41xxGiuModel kModel = {
    /*base=*/0x0B000100u,
    /*size=*/0x20u,
    /* GIUPODATL RTCRST and Other-resets columns: every bit 1 (UM 18.2.15). */
    /*podat_l_power_on=*/0xFFFFu,
    /* "'1' is set to the corresponding INTS bit when '1' is set to the corresponding INTE
       bit in the GIUINTENL register and when the signal input to an interrupt-enabled GPIO
       pin meets the conditions set via the GIUNTTYPL register ... and the GIUINTALSELL
       register" (UM 18.2.5, 18.2.6). */
    /*intstat_sets_while_disabled=*/false,
    /* INTE is a term of the INTS set condition (UM 18.2.5, 18.2.6); the ICU's GIUINTLREG
       carries those INTS bits (UM 14.2.5). */
    /*inten_gates_icu_input=*/false,
    /* GIUPODATL Other-resets row: every bit 1 (UM 18.2.15). */
    /*podat_l_retained_on_reset=*/false,
    /* GIUPODATH Other-resets row: every bit 0 (UM 18.2.16 p380). */
    /*podat_h_retained_on_reset=*/false,
};

class Vr4102Giu : public Vr41xxGiuBase<SocId::Vr4102, kModel> {
public:
    using Vr41xxGiuBase::Vr41xxGiuBase;
};

}  /* namespace */

REGISTER_SERVICE_AS(Vr4102Giu, Vr41xxGiu);
