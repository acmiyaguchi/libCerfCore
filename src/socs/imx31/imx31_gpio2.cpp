#include "../freescale_gpio_impl.h"
#include "imx31_id.h"

namespace {

/* MCIMX31RM Table 5-3: GPIO2 at 0x53FD_0000. */
class Imx31Gpio2
    : public cerf_freescale_gpio_detail::FreescaleGpioBase<0x53FD0000u,
                                                           SocId::Imx31> {
    using FreescaleGpioBase::FreescaleGpioBase;
};

}  /* namespace */

REGISTER_SERVICE(Imx31Gpio2);
