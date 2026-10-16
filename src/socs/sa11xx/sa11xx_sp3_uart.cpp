#include "sa11xx_sp3_uart.h"

#include <cerfcore/core/cerf_emulator.h>
#include "sa11xx_sp3_uart_receive_fifo.h"
#include "sa11xx_sp3_uart_transmit_fifo.h"

namespace cerfcore {

Sa11xxDmaTransmitFifo& Sa11xxSp3Uart::TransmitFifo() {
    return emu_.Get<Sa11xxSp3UartTransmitFifo>();
}

Sa11xxDmaReceiveFifo& Sa11xxSp3Uart::ReceiveFifo() {
    return emu_.Get<Sa11xxSp3UartReceiveFifo>();
}

REGISTER_SERVICE(Sa11xxSp3Uart);

}
