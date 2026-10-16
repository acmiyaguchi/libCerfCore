#pragma once

#include "sa11xx_uart_base.h"

namespace cerfcore {

/* iPaq H3xxx wires MicroP via SP1; MicroP service installs a TX
   listener and pushes RX bytes through Sa11xxUartBase's hooks. */

class Sa11xxSp1Uart : public Sa11xxUartBase {
public:
    using Sa11xxUartBase::Sa11xxUartBase;

    uint32_t MmioBase() const override { return 0x80010000u; }

protected:
    const char* ChannelName()  const override { return "UART1"; }
    /* Linux arch/arm/mach-sa1100 irqs.h: IRQ_Ser1UART maps to
       SA-1110 INTC source bit 15. h3xxx.c routes MicroP IRQ here. */
    int         IntcSourceBit() const override { return 15; }
    /* SA-1110 Table 11-5 (printed 11-5): serial port 1 GPIO 18 "Sample clock input to UART". */
    int         SampleClockPin() const override { return 18; }
    /* SA-1110 Table 11-6 (printed 11-9): Serial port 1 UART transmit DS 0100, receive DS 0101. */
    uint32_t    TransmitDeviceSelect() const override { return 0x4u; }
    uint32_t    ReceiveDeviceSelect() const override { return 0x5u; }
    Sa11xxDmaTransmitFifo& TransmitFifo() override;
    Sa11xxDmaReceiveFifo&  ReceiveFifo() override;
};

}
