#pragma once

#include "sa11xx_dma_transmit_fifo.h"

class Sa11xxSspTransmitFifo : public Sa11xxDmaTransmitFifo {
public:
    explicit Sa11xxSspTransmitFifo(CerfEmulator& emu);
};
