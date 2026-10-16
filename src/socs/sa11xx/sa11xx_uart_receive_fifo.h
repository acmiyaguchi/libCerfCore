#pragma once

#include "sa11xx_dma_receive_fifo.h"

class Sa11xxUartReceiveFifo : public Sa11xxDmaReceiveFifo {
public:
    explicit Sa11xxUartReceiveFifo(CerfEmulator& emu);
};
