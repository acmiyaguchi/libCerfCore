#pragma once

#include <cerfcore/core/service.h>
#include "../dma_burst_fifo.h"
#include "../pxa2xx/pxa2xx_frame_slot_law.h"

#include <cstdint>
#include <deque>
#include <utility>
#include <vector>

class StateReader;
class StateWriter;

class Pxa255I2sFifoValues : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    void     TxClear();
    void     TxPut(uint32_t value);
    void     TxLoadBlock(const uint8_t* bytes, uint32_t length, uint64_t base);
    void     TxTake(const DmaBurstFifo& fifo, const DmaBurstFifo::Arrivals& arrivals, uint64_t supplied_from,
                    uint64_t departures, std::vector<uint32_t>* out);
    void     ClearLastSent() { last_sent_ = 0u; }

    void     RxClear();
    uint32_t RxPop();
    void     RxPush(const DmaBurstFifo& fifo, const DmaBurstFifo::Arrivals& arrivals, uint64_t kept,
                    const std::vector<uint32_t>* values);
    void     RxWriteBlock(uint8_t* dst, uint32_t bytes);

    void     LoopRecord(const Pxa2xxFrameSlotLaw& law, uint64_t first, uint64_t slots,
                        const std::vector<uint32_t>& departed);
    void     LoopTake(const Pxa2xxFrameSlotLaw& law, uint64_t first, uint64_t slots, std::vector<uint32_t>& out);
    void     LoopDrop(uint64_t frames);
    void     LoopClear() { loop_.clear(); }

    void Save(StateWriter& w);
    void Restore(StateReader& r);

private:
    static constexpr uint32_t kRing       = 32u;
    static constexpr uint32_t kBlockWords = 2048u;

    uint32_t BlockWord(uint64_t index) const;
    uint32_t LoopValue(uint64_t frame);
    void     RingPush(uint32_t* ring, uint32_t size, uint32_t head, uint32_t& count, uint32_t value);
    uint32_t RingPop(const uint32_t* ring, uint32_t size, uint32_t& head, uint32_t& count);

    uint32_t tx_[kRing]           = {};
    uint32_t tx_head_             = 0;
    uint32_t tx_count_            = 0;
    uint32_t block_[kBlockWords]  = {};
    uint64_t block_base_          = 0;
    uint32_t block_words_         = 0;
    uint32_t last_sent_           = 0;
    uint32_t rx_[kRing]           = {};
    uint32_t rx_head_             = 0;
    uint32_t rx_count_            = 0;
    uint32_t out_[kBlockWords]    = {};
    uint32_t out_head_            = 0;
    uint32_t out_count_           = 0;
    std::deque<std::pair<uint64_t, uint32_t>> loop_;
};
