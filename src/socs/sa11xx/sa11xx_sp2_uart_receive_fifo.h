#pragma once

#include "sa11xx_uart_receive_fifo.h"

class Sa11xxSp2UartReceiveFifo final : public Sa11xxUartReceiveFifo {
public:
    using Sa11xxUartReceiveFifo::Sa11xxUartReceiveFifo;
};
