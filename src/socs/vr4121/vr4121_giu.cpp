#include "../vr41xx/vr41xx_giu_impl.h"

#include <cstdint>
#include "vr4121_id.h"

namespace {

using cerf_vr41xx_giu_detail::Vr41xxGiuBase;
using cerf_vr41xx_giu_detail::Vr41xxGiuModel;

/* VR4121 GIU (UM Table 1-9): GIUIOSELL..GIUPODATH at 0x0B000100-0x0B00011E, plus
   GIUUSEUPDN/GIUTERMUPDN in a second window at 0x0B0002E0-0x0B0002E2. */
constexpr Vr41xxGiuModel kModel = {
    /*base=*/0x0B000100u,
    /*size=*/0x20u,
    /* GIUPODATL RTCRST column: D15:12 = 0, D11:0 = 1 (UM 19.2.15). */
    /*podat_l_power_on=*/0x0FFFu,
    /* "'1' is set to the corresponding INTS bit when the signal input to the GPIO pin
       meets the condition set via the GIUINTTYPL register ... or the GIUINTALSELL
       register" (UM 19.2.5, 19.2.6). */
    /*intstat_sets_while_disabled=*/true,
    /* "Even if the corresponding bit is set to '1', however, no interrupt occurs when the
       GIUINTENL register (0x0B00 010C: GPIO Interrupt Enable Register) is set to prohibit
       interrupt" (UM 19.2.5; UM 19.2.6 says the same of GIUINTENH). */
    /*inten_gates_icu_input=*/true,
    /* GIUPODATL After-reset row: "Previous value is retained" (UM 19.2.15). */
    /*podat_l_retained_on_reset=*/true,
    /* GIUPODATH After-reset row: "Previous value is retained" (UM 19.2.16 p464). */
    /*podat_h_retained_on_reset=*/true,
};

class Vr4121Giu : public Vr41xxGiuBase<SocId::Vr4121, kModel> {
public:
    using Vr41xxGiuBase::Vr41xxGiuBase;
};

}  /* namespace */

REGISTER_SERVICE_AS(Vr4121Giu, Vr41xxGiu);
