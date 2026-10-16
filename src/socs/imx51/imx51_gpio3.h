#pragma once

#include "../freescale_gpio_impl.h"
#include "imx51_id.h"

/* i.MX51 GPIO3 at 0x73F8_C000 (MCIMX51RM Table 2-2). Named (not anonymous) so the
   Ford board can drive its GPIO3.24 EOL/factory-jig strap, read by the IPL. */
class Imx51Gpio3
    : public cerf_freescale_gpio_detail::FreescaleGpioBase<0x73F8C000u,
                                                           SocId::Imx51> {
    using FreescaleGpioBase::FreescaleGpioBase;
};
