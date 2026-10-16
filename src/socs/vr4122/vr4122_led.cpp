#include "../vr41xx/vr41xx_led_impl.h"
#include "vr4122_id.h"

namespace {

using cerf_vr41xx_led_detail::Vr41xxLedBase;

/* VR4122 LED at 0x0F000180 (VR4131 UM U15350EJ2V0UM ch.19; NetBSD vripreg.h
   VR4122_LED_ADDR): LEDHTSREG 0x180, LEDLTSREG 0x182, LEDCNTREG 0x188, LEDASTCREG
   0x18A, LEDINTREG 0x18C. */
class Vr4122Led : public Vr41xxLedBase<SocId::Vr4122, 0x0F000180u> {
public:
    using Vr41xxLedBase::Vr41xxLedBase;
};

}  /* namespace */

REGISTER_SERVICE(Vr4122Led);
