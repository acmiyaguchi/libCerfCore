#include "sa11xx_sp1_uart.h"

#include <cerfcore/core/cerf_emulator.h>
#include "sa11xx_sp1_uart_receive_fifo.h"
#include "sa11xx_sp1_uart_transmit_fifo.h"

Sa11xxDmaTransmitFifo& Sa11xxSp1Uart::TransmitFifo() {
    return emu_.Get<Sa11xxSp1UartTransmitFifo>();
}

Sa11xxDmaReceiveFifo& Sa11xxSp1Uart::ReceiveFifo() {
    return emu_.Get<Sa11xxSp1UartReceiveFifo>();
}

REGISTER_SERVICE(Sa11xxSp1Uart);
