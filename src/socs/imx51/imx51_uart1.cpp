#include "imx51_uart1.h"

#include <cerfcore/core/cerf_emulator.h>
#include "../irq_controller.h"

namespace cerfcore {

/* MCIMX51RM Table 3-2 (page 3-2): IRQ 31 "UART-1 ORed interrupt". */
void Imx51Uart1::DriveIrq(bool level) {
    auto& ic = emu_.Get<IrqController>();
    if (level) ic.AssertIrq(31);
    else       ic.DeAssertIrq(31);
}

REGISTER_SERVICE(Imx51Uart1);

}
