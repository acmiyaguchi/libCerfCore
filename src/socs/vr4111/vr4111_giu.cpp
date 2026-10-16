#include "../vr41xx/vr41xx_giu_impl.h"

#include <cstdint>
#include "vr4111_id.h"

namespace {

using cerf_vr41xx_giu_detail::Vr41xxGiuBase;
using cerf_vr41xx_giu_detail::Vr41xxGiuModel;

/* VR4111 GIU (UM Table 19-2 p398): GIUIOSELL..GIUPODATH at 0x0B000100-0x0B00011E,
   plus GIUUSEUPDN/GIUTERMUPDN at 0x0B0002E0-0x0B0002E2. */
constexpr Vr41xxGiuModel kModel = {
    /*base=*/0x0B000100u,
    /*size=*/0x20u,
    /* GIUPODATL RTCRST column: D11:0 = 1, D15:12 = 0 (UM 19.2.15 p414). */
    /*podat_l_power_on=*/0x0FFFu,
    /* "'1' is set to the corresponding INTS bit when the signal input to the GPIO pin
       meets the condition set via the GIUINTTYPL register ... or the GIUINTALSELL
       register" (UM 19.2.5 p403, and 19.2.6 for the H half). */
    /*intstat_sets_while_disabled=*/true,
    /* "Even if the corresponding bit is set to '1', however, no interrupt occurs when the
       GIUINTENL register (0x0B00 010C) is set to prohibit interrupt" (UM 19.2.5 p403). */
    /*inten_gates_icu_input=*/true,
    /* GIUPODATL Other-resets row: "Previous value is retained" (UM 19.2.15 p414). */
    /*podat_l_retained_on_reset=*/true,
    /* GIUPODATH Other-resets row: "Previous value is retained" (UM 19.2.16 p416). */
    /*podat_h_retained_on_reset=*/true,
};

class Vr4111Giu : public Vr41xxGiuBase<SocId::Vr4111, kModel> {
public:
    using Vr41xxGiuBase::Vr41xxGiuBase;
};

}  /* namespace */

REGISTER_SERVICE_AS(Vr4111Giu, Vr41xxGiu);
