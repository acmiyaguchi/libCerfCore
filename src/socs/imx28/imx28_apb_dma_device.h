#pragma once

#include <cstdint>

namespace cerfcore {

/* MCIMX28RM Rev. 2 sections 6.2 and 7.2: the device behind one DMA channel. */
class Imx28ApbDmaDevice {
public:
    struct Transfer {
        uint32_t bytes;
        bool     terminated;
        bool     deferred = false;
    };

    virtual ~Imx28ApbDmaDevice() = default;

    virtual void     DmaPioWrite(uint32_t channel, uint32_t index, uint32_t value) = 0;
    virtual Transfer DmaToMemory(uint32_t channel, uint8_t* dst, uint32_t count) = 0;
    virtual Transfer DmaFromMemory(uint32_t channel, const uint8_t* src, uint32_t count) = 0;
    virtual bool     DmaCommandEnded(uint32_t channel) = 0;
    virtual void     DmaChannelReset(uint32_t channel) = 0;
};

}
