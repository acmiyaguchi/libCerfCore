#include "imx51_uart3.h"

#include <cerfcore/core/cerf_emulator.h>
#include "../irq_controller.h"

/* MCIMX51RM Table 3-2 (page 3-2): IRQ 33 "UART-3 ORed interrupt". */
void Imx51Uart3::DriveIrq(bool level) {
    auto& ic = emu_.Get<IrqController>();
    if (level) ic.AssertIrq(33);
    else       ic.DeAssertIrq(33);
}

REGISTER_SERVICE(Imx51Uart3);
