#define NOMINMAX

#include "pxa255_i2s_fifo_values.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/state/state_stream.h>
#include "pxa255_id.h"

#include <algorithm>
#include <cstring>

namespace cerfcore {

REGISTER_SERVICE(Pxa255I2sFifoValues);

namespace {

template <typename F>
void ForEachMoved(const DmaBurstFifo& fifo, const DmaBurstFifo::Arrivals& arrivals, uint64_t from, F&& fn) {
    uint64_t index = from;
    for (uint64_t k = 0; k < arrivals.refill_words; ++k) fn(index++, k < arrivals.refill_landed);
    for (uint64_t j = 0; j < arrivals.bursts; ++j) {
        const uint64_t size = j + 1u < arrivals.bursts ? fifo.Burst() : arrivals.last_words;
        const uint64_t land = std::min(fifo.Landing(), size);
        for (uint64_t k = 0; k < size; ++k) fn(index++, k < land);
    }
}

}  // namespace

bool Pxa255I2sFifoValues::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Pxa255;
}

void Pxa255I2sFifoValues::RingPush(uint32_t* ring, uint32_t size, uint32_t head, uint32_t& count, uint32_t value) {
    if (count == size) emu_.Get<Fatal>().Die("Pxa255I2sFifoValues: a %u-entry value ring overflowed", size);
    ring[(head + count) % size] = value;
    ++count;
}

uint32_t Pxa255I2sFifoValues::RingPop(const uint32_t* ring, uint32_t size, uint32_t& head, uint32_t& count) {
    if (count == 0u) return 0u;
    const uint32_t value = ring[head];
    head = (head + 1u) % size;
    --count;
    return value;
}

uint32_t Pxa255I2sFifoValues::BlockWord(uint64_t index) const {
    const uint64_t k = index - block_base_;
    return k < block_words_ ? block_[k] : 0u;
}

void Pxa255I2sFifoValues::TxClear() {
    tx_head_  = 0u;
    tx_count_ = 0u;
}

void Pxa255I2sFifoValues::TxPut(uint32_t value) { RingPush(tx_, kRing, tx_head_, tx_count_, value); }

void Pxa255I2sFifoValues::TxLoadBlock(const uint8_t* bytes, uint32_t length, uint64_t base) {
    const uint32_t words = (length + 3u) / 4u;
    if (words > kBlockWords) {
        emu_.Get<Fatal>().Die("Pxa255I2sFifoValues: a %u-byte transmit block exceeds %u words", length, kBlockWords);
    }
    for (uint32_t i = 0; i < words; ++i) {
        uint8_t w[4] = {};
        std::memcpy(w, bytes + 4u * i, std::min(4u, length - 4u * i));
        block_[i] = cerf::le::U32(w);
    }
    block_words_ = words;
    block_base_  = base;
}

void Pxa255I2sFifoValues::TxTake(const DmaBurstFifo& fifo, const DmaBurstFifo::Arrivals& arrivals,
                                 uint64_t supplied_from, uint64_t departures, std::vector<uint32_t>* out) {
    uint64_t   left   = departures;
    const auto depart = [&](uint32_t value) {
        last_sent_ = value;
        if (out != nullptr) out->push_back(value);
        --left;
    };
    while (left != 0u && tx_count_ != 0u) depart(RingPop(tx_, kRing, tx_head_, tx_count_));
    ForEachMoved(fifo, arrivals, supplied_from, [&](uint64_t index, bool landed) {
        if (!landed) return;
        if (left != 0u) {
            depart(BlockWord(index));
        } else {
            RingPush(tx_, kRing, tx_head_, tx_count_, BlockWord(index));
        }
    });
    if (left != 0u) {
        emu_.Get<Fatal>().Die("Pxa255I2sFifoValues: %llu transmit departures exceed the FIFO and its supply",
                              static_cast<unsigned long long>(departures));
    }
}

void Pxa255I2sFifoValues::RxClear() {
    rx_head_  = 0u;
    rx_count_ = 0u;
}

uint32_t Pxa255I2sFifoValues::RxPop() { return RingPop(rx_, kRing, rx_head_, rx_count_); }

void Pxa255I2sFifoValues::RxPush(const DmaBurstFifo& fifo, const DmaBurstFifo::Arrivals& arrivals, uint64_t kept,
                                 const std::vector<uint32_t>* values) {
    uint64_t p = 0;
    ForEachMoved(fifo, arrivals, 0u, [&](uint64_t, bool landed) {
        uint32_t value = 0u;
        if (landed && rx_count_ != 0u) {
            value = RingPop(rx_, kRing, rx_head_, rx_count_);
        } else if (landed && p < kept) {
            value = values != nullptr ? (*values)[static_cast<size_t>(p)] : 0u;
            ++p;
        } else if (landed) {
            emu_.Get<Fatal>().Die("Pxa255I2sFifoValues: a receive DMA read found no word the FIFO count holds");
        }
        RingPush(out_, kBlockWords, out_head_, out_count_, value);
    });
    for (; p < kept; ++p) {
        RingPush(rx_, kRing, rx_head_, rx_count_, values != nullptr ? (*values)[static_cast<size_t>(p)] : 0u);
    }
}

void Pxa255I2sFifoValues::RxWriteBlock(uint8_t* dst, uint32_t bytes) {
    for (uint32_t off = 0; off < bytes; off += 4u) {
        uint8_t w[4];
        cerf::le::Put32(w, RingPop(out_, kBlockWords, out_head_, out_count_));
        std::memcpy(dst + off, w, std::min(4u, bytes - off));
    }
}

/* Intel PXA255 Developer's Manual section 14.3.4 (page 14-5): "During Transmit Under-run conditions, the last
   valid sample is continuously sent out across the I2SLINK." */
void Pxa255I2sFifoValues::LoopRecord(const Pxa2xxFrameSlotLaw& law, uint64_t first, uint64_t slots,
                                     const std::vector<uint32_t>& departed) {
    uint64_t frame = 0;
    for (uint64_t i = 0; i < slots; ++i) {
        const uint32_t sent = i < departed.size() ? departed[static_cast<size_t>(i)] : last_sent_;
        if (law.FrameOfSlot(first + i + 1u, frame)) loop_.emplace_back(frame, sent);
    }
}

void Pxa255I2sFifoValues::LoopTake(const Pxa2xxFrameSlotLaw& law, uint64_t first, uint64_t slots,
                                   std::vector<uint32_t>& out) {
    uint64_t frame = 0;
    for (uint64_t i = 0; i < slots; ++i) out.push_back(law.FrameOfSlot(first + i + 1u, frame) ? LoopValue(frame) : 0u);
}

uint32_t Pxa255I2sFifoValues::LoopValue(uint64_t frame) {
    LoopDrop(frame);
    if (loop_.empty() || loop_.front().first != frame) return 0u;
    const uint32_t value = loop_.front().second;
    loop_.pop_front();
    return value;
}

void Pxa255I2sFifoValues::LoopDrop(uint64_t frames) {
    while (!loop_.empty() && loop_.front().first < frames) loop_.pop_front();
}

void Pxa255I2sFifoValues::Save(StateWriter& w) {
    w.WriteBytes("i2s_tx_values", tx_, sizeof(tx_));
    w.Write<uint32_t>("i2s_tx_head", tx_head_);
    w.Write<uint32_t>("i2s_tx_count", tx_count_);
    w.WriteBytes("i2s_tx_block", block_, sizeof(block_));
    w.Write<uint64_t>("i2s_tx_block_base", block_base_);
    w.Write<uint32_t>("i2s_tx_block_words", block_words_);
    w.Write<uint32_t>("i2s_tx_last", last_sent_);
    w.WriteBytes("i2s_rx_values", rx_, sizeof(rx_));
    w.Write<uint32_t>("i2s_rx_head", rx_head_);
    w.Write<uint32_t>("i2s_rx_count", rx_count_);
    w.WriteBytes("i2s_rx_out", out_, sizeof(out_));
    w.Write<uint32_t>("i2s_rx_out_head", out_head_);
    w.Write<uint32_t>("i2s_rx_out_count", out_count_);
    w.Write<uint32_t>("i2s_loop_count", static_cast<uint32_t>(loop_.size()));
    for (const auto& e : loop_) {
        w.Write<uint64_t>("i2s_loop_frame", e.first);
        w.Write<uint32_t>("i2s_loop_value", e.second);
    }
}

void Pxa255I2sFifoValues::Restore(StateReader& r) {
    uint32_t loop_count = 0;
    r.ReadBytes("i2s_tx_values", tx_, sizeof(tx_));
    r.Read("i2s_tx_head", tx_head_);
    r.Read("i2s_tx_count", tx_count_);
    r.ReadBytes("i2s_tx_block", block_, sizeof(block_));
    r.Read("i2s_tx_block_base", block_base_);
    r.Read("i2s_tx_block_words", block_words_);
    r.Read("i2s_tx_last", last_sent_);
    r.ReadBytes("i2s_rx_values", rx_, sizeof(rx_));
    r.Read("i2s_rx_head", rx_head_);
    r.Read("i2s_rx_count", rx_count_);
    r.ReadBytes("i2s_rx_out", out_, sizeof(out_));
    r.Read("i2s_rx_out_head", out_head_);
    r.Read("i2s_rx_out_count", out_count_);
    r.Read("i2s_loop_count", loop_count);
    loop_.clear();
    for (uint32_t i = 0; i < loop_count; ++i) {
        uint64_t frame = 0;
        uint32_t value = 0;
        r.Read("i2s_loop_frame", frame);
        r.Read("i2s_loop_value", value);
        loop_.emplace_back(frame, value);
    }
}

}
