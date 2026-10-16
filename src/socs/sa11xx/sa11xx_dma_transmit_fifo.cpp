#include "sa11xx_dma_transmit_fifo.h"

#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/boards/board_context.h>
#include "sa1110_id.h"
#include "sa1100_id.h"
#include <cerfcore/cpu/emulated_memory.h>
#include <cerfcore/state/state_stream.h>
#include "sa11xx_dma.h"
#include "sa11xx_dma_port.h"

namespace cerfcore {

bool Sa11xxDmaTransmitFifo::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && (bd->GetSocId() == SocId::Sa1110 || bd->GetSocId() == SocId::Sa1100);
}

void Sa11xxDmaTransmitFifo::OnReady() {
    dma_ = &emu_.Get<Sa11xxDma>();
    mem_ = &emu_.Get<EmulatedMemory>();
    fifo_.Configure(geometry_.depth, geometry_.request_at, geometry_.burst);
}

void Sa11xxDmaTransmitFifo::Pull() {
    while (pulled_ < fifo_.Moved()) {
        uint32_t pa = 0;
        if (!dma_->SourceAddress(port_, pulled_, pa)) {
            emu_.Get<Fatal>().Die("Sa11xxDmaTransmitFifo: the transmit DMA moved word %llu that no "
                                  "started buffer holds", static_cast<unsigned long long>(pulled_));
        }
        uint8_t word[2] = {};
        mem_->CopyOut(pa, word, port_->DatumBytes());
        values_.push_back(static_cast<uint16_t>(cerf::le::U16(word) & geometry_.data_mask));
        ++pulled_;
    }
}

void Sa11xxDmaTransmitFifo::Clear() {
    fifo_.Clear();
    fifo_.SetSupply(0u);
    values_.clear();
    pulled_ = fifo_.Moved();
}

bool Sa11xxDmaTransmitFifo::Put(uint16_t value) {
    if (fifo_.Level() >= geometry_.depth) return false;
    fifo_.Put();
    values_.push_back(static_cast<uint16_t>(value & geometry_.data_mask));
    return true;
}

void Sa11xxDmaTransmitFifo::SetSupply(uint64_t words) {
    fifo_.SetSupply(words);
    fifo_.Refill();
    Pull();
}

uint64_t Sa11xxDmaTransmitFifo::Take(uint64_t n, std::vector<uint16_t>& taken) {
    const uint64_t empty = fifo_.Take(n);
    Pull();
    for (uint64_t i = 0; i < n - empty; ++i) {
        shifter_ = values_.front();
        values_.pop_front();
        taken.push_back(shifter_);
    }
    return empty;
}

void Sa11xxDmaTransmitFifo::Save(StateWriter& w) const {
    w.Write<uint32_t>("dma_tx_level", fifo_.Level());
    w.Write<uint64_t>("dma_tx_moved", fifo_.Moved());
    w.Write<uint64_t>("dma_tx_pulled", pulled_);
    w.Write<uint16_t>("dma_tx_shifter", shifter_);
    w.Write<uint32_t>("dma_tx_value_count", static_cast<uint32_t>(values_.size()));
    for (uint16_t v : values_) w.Write("dma_tx_value", v);
}

void Sa11xxDmaTransmitFifo::Restore(StateReader& r) {
    uint32_t level = 0, n = 0;
    uint64_t moved = 0;
    r.Read("dma_tx_level", level);
    r.Read("dma_tx_moved", moved);
    r.Read("dma_tx_pulled", pulled_);
    r.Read("dma_tx_shifter", shifter_);
    r.Read("dma_tx_value_count", n);
    values_.clear();
    for (uint32_t i = 0; i < n; ++i) {
        uint16_t v = 0;
        r.Read("dma_tx_value", v);
        values_.push_back(v);
    }
    fifo_.Restore(level, moved);
    fifo_.SetSupply(0u);
}

}
