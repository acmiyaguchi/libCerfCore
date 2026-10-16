#include "../freescale_gpio_impl.h"
#include "imx51_id.h"

namespace {

/* MCIMX51RM Table 2-2: GPIO2 at 0x73F8_8000. */
class Imx51Gpio2
    : public cerf_freescale_gpio_detail::FreescaleGpioBase<0x73F88000u,
                                                           SocId::Imx51> {
    using FreescaleGpioBase::FreescaleGpioBase;
};

}  /* namespace */

REGISTER_SERVICE(Imx51Gpio2);
