#pragma once

#include "pxa255_uart16550.h"

/* PXA255 BTUART - Bluetooth UART (base 0x40200000, INTC IS21 Table 4-35).
   Exposed via a header so a board input transport can resolve it and inject RX
   bytes through Uart16550::PushRx - the NEC MobilePro 900 PCO keyboard/touch
   companion streams its reports into the BTUART RX FIFO this way. */
class Pxa255Btuart : public Pxa255Uart16550 {
public:
    using Pxa255Uart16550::Pxa255Uart16550;

    uint32_t MmioBase() const override { return 0x40200000u; }

protected:
    uint32_t    IntcBit() const override { return 21u; }
    const char* Name()    const override { return "BTUART"; }
};
