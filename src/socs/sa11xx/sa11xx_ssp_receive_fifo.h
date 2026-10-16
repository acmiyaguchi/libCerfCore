#pragma once

#include "sa11xx_dma_receive_fifo.h"

namespace cerfcore {

class Sa11xxSspReceiveFifo : public Sa11xxDmaReceiveFifo {
public:
    explicit Sa11xxSspReceiveFifo(CerfEmulator& emu);
};

}
