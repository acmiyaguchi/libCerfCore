#include "sa11xx_uart_base.h"

#include <cerfcore/core/cerf_emulator.h>
#include "sa11xx_sp2_uart_receive_fifo.h"
#include "sa11xx_sp2_uart_transmit_fifo.h"

namespace {

/* SA-1110 §11.10 SP2 UART. IrDA-capable port - on iPaq H3xxx this
   surface is reused by the kernel's exception/init path. */

class Sa11xxSp2Uart : public Sa11xxUartBase {
public:
    using Sa11xxUartBase::Sa11xxUartBase;

    uint32_t MmioBase() const override { return 0x80030000u; }

protected:
    const char* ChannelName() const override { return "UART2"; }
    /* SA-1110 Table 11-6 (printed 11-9): Serial port 2 UART transmit DS 0110, receive DS 0111. */
    uint32_t    TransmitDeviceSelect() const override { return 0x6u; }
    uint32_t    ReceiveDeviceSelect() const override { return 0x7u; }
    Sa11xxDmaTransmitFifo& TransmitFifo() override {
        return emu_.Get<Sa11xxSp2UartTransmitFifo>();
    }
    Sa11xxDmaReceiveFifo& ReceiveFifo() override {
        return emu_.Get<Sa11xxSp2UartReceiveFifo>();
    }
};

}  /* namespace */

REGISTER_SERVICE(Sa11xxSp2Uart);
