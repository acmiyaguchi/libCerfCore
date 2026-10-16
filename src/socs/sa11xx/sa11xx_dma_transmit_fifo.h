#pragma once

#include <cerfcore/core/service.h>
#include "../dma_burst_fifo.h"

#include <cstdint>
#include <deque>
#include <vector>

namespace cerfcore {

class EmulatedMemory;
class Sa11xxDma;
class Sa11xxDmaPort;
class StateReader;
class StateWriter;

class Sa11xxDmaTransmitFifo : public Service {
public:
    bool ShouldRegister() override;
    void OnReady() override;

    void     Attach(const Sa11xxDmaPort* port) { port_ = port; }
    void     Clear();
    bool     Put(uint16_t value);
    void     SetSupply(uint64_t words);
    uint64_t Take(uint64_t n, std::vector<uint16_t>& taken);

    uint16_t Shifter() const { return shifter_; }
    uint16_t DataMask() const { return geometry_.data_mask; }
    uint32_t Level() const { return fifo_.Level(); }
    uint64_t Moved() const { return fifo_.Moved(); }
    uint64_t TakesBeforeEmpty() const { return fifo_.TakesBeforeEmpty(); }
    bool     TakesToMove(uint64_t words, uint64_t& n) const { return fifo_.TakesToMove(words, n); }

    void Save(StateWriter& w) const;
    void Restore(StateReader& r);

protected:
    struct Geometry {
        uint32_t depth      = 0;
        uint32_t request_at = 0;
        uint32_t burst      = 0;
        uint16_t data_mask  = 0;
    };

    Sa11xxDmaTransmitFifo(CerfEmulator& emu, Geometry geometry)
        : Service(emu), geometry_(geometry) {}

private:
    void Pull();

    const Geometry       geometry_;
    Sa11xxDma*           dma_  = nullptr;
    EmulatedMemory*      mem_  = nullptr;
    const Sa11xxDmaPort* port_ = nullptr;
    DmaBurstFifo         fifo_;
    std::deque<uint16_t> values_;
    uint64_t             pulled_  = 0;
    uint16_t             shifter_ = 0;
};

}
