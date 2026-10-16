#pragma once

#include "sa11xx_dma_transmit_fifo.h"

class Sa11xxUartTransmitFifo : public Sa11xxDmaTransmitFifo {
public:
    explicit Sa11xxUartTransmitFifo(CerfEmulator& emu);
};
