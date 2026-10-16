#include "imx51_uart2.h"

#include <cerfcore/core/cerf_emulator.h>
#include "../irq_controller.h"

/* MCIMX51RM Table 3-2 (page 3-2): IRQ 32 "UART-2 ORed interrupt". */
void Imx51Uart2::DriveIrq(bool level) {
    auto& ic = emu_.Get<IrqController>();
    if (level) ic.AssertIrq(32);
    else       ic.DeAssertIrq(32);
}

REGISTER_SERVICE(Imx51Uart2);
