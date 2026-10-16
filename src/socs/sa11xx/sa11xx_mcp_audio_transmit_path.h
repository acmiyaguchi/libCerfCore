#pragma once

#include "sa11xx_dma_transmit_fifo.h"

class Sa11xxMcpAudioTransmitPath : public Sa11xxDmaTransmitFifo {
public:
    explicit Sa11xxMcpAudioTransmitPath(CerfEmulator& emu);
};
