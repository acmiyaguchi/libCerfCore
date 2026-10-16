#include "../freescale_gpio_impl.h"
#include "imx31_id.h"

namespace {

/* MCIMX31RM Table 5-3: GPIO3 at 0x53FA_4000. */
class Imx31Gpio3
    : public cerf_freescale_gpio_detail::FreescaleGpioBase<0x53FA4000u,
                                                           SocId::Imx31> {
    using FreescaleGpioBase::FreescaleGpioBase;
};

}  /* namespace */

REGISTER_SERVICE(Imx31Gpio3);
