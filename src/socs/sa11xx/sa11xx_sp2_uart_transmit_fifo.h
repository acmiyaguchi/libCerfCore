#pragma once

#include "sa11xx_uart_transmit_fifo.h"

namespace cerfcore {

class Sa11xxSp2UartTransmitFifo final : public Sa11xxUartTransmitFifo {
public:
    using Sa11xxUartTransmitFifo::Sa11xxUartTransmitFifo;
};

}
