#pragma once

#include <cstdint>

namespace cerfcore {

struct FreescaleSsiSectionControl {
    bool     enabled     = false;
    bool     run         = false;
    bool     two_channel = false;
    bool     dma         = false;
    bool     fen[2]      = {false, false};
    uint32_t wm[2]       = {0u, 0u};
    uint32_t data        = 0;
    bool     network     = false;
};

class FreescaleSsiDmaPort {
public:
    virtual ~FreescaleSsiDmaPort() = default;

    virtual void     SetDmaBurst(uint32_t words) = 0;
    virtual void     SetDmaClock(bool running) = 0;
    virtual uint64_t DmaWords() = 0;
    virtual bool     CycleOfDmaWords(uint64_t words, uint64_t& cycle) = 0;
};

}
