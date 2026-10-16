#pragma once

#include "../freescale_uart_impl.h"
#include <cerfcore/boards/board_context.h>
#include "imx51_id.h"

namespace cerfcore {

class Imx51Uart1
    : public cerf_freescale_uart_detail::FreescaleUartBase<0x73FBC000u, 1,
                                                           SocId::Imx51> {
public:
    using FreescaleUartBase::FreescaleUartBase;

protected:
    void DriveIrq(bool level) override;
};

}
