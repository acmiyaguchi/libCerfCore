#include "../vr41xx/vr41xx_led_impl.h"
#include "vr4102_id.h"

namespace {

using cerf_vr41xx_led_detail::Vr41xxLedBase;

/* VR4102 LED at 0x0B000240 (UM Table 23-1, p.453). */
class Vr4102Led : public Vr41xxLedBase<SocId::Vr4102, 0x0B000240u> {
public:
    using Vr41xxLedBase::Vr41xxLedBase;
};

}  /* namespace */

REGISTER_SERVICE(Vr4102Led);
