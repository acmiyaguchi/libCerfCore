#pragma once

#include "../freescale_gpio_impl.h"
#include "imx51_id.h"

/* i.MX51 GPIO4 at 0x73F9_0000 (MCIMX51RM Table 2-2). Named (not anonymous) so the
   Ford iPod-auth coprocessor can drive its SOMI-ready input via SetInputPin: the
   ipdacp driver's DDKGpioReadDataPin(port 3, pin 23) resolves to GPIO4 (cspddk's
   port table is 0-based - port 3 -> 0x73F90000), NOT GPIO3. */
class Imx51Gpio4
    : public cerf_freescale_gpio_detail::FreescaleGpioBase<0x73F90000u,
                                                           SocId::Imx51> {
    using FreescaleGpioBase::FreescaleGpioBase;
};
