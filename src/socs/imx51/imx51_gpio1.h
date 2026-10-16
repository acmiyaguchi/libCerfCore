#pragma once

#include "../freescale_gpio_impl.h"
#include "imx51_id.h"

/* GPIO1 at 0x73F8_4000 (MCIMX51RM Table 2-2); its OR'd pin-0..15 / 16..31 interrupt
   lines are TZIC sources 50 / 51 (MCIMX51RM Table 3-2). */
class Imx51Gpio1
    : public cerf_freescale_gpio_detail::FreescaleGpioBase<0x73F84000u,
                                                           SocId::Imx51, 50, 51> {
    using FreescaleGpioBase::FreescaleGpioBase;
};
