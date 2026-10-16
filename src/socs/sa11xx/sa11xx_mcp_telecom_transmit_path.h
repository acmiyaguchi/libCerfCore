#pragma once

#include "sa11xx_dma_transmit_fifo.h"

class Sa11xxMcpTelecomTransmitPath : public Sa11xxDmaTransmitFifo {
public:
    explicit Sa11xxMcpTelecomTransmitPath(CerfEmulator& emu);
};
