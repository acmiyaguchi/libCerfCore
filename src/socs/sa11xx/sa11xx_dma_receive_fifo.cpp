#include "sa11xx_dma_receive_fifo.h"

#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/boards/board_context.h>
#include "sa1110_id.h"
#include "sa1100_id.h"
#include <cerfcore/cpu/emulated_memory.h>
#include <cerfcore/state/state_stream.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "sa11xx_dma_port.h"

#include <vector>

namespace cerfcore {

bool Sa11xxDmaReceiveFifo::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && (bd->GetSocId() == SocId::Sa1110 || bd->GetSocId() == SocId::Sa1100);
}

void Sa11xxDmaReceiveFifo::OnReady() {
    mem_ = &emu_.Get<EmulatedMemory>();
    fifo_.Configure(geometry_.depth, geometry_.depth - geometry_.request_at, geometry_.burst);
    fifo_.Restore(geometry_.depth, 0u);
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) { out_.clear(); });
}

void Sa11xxDmaReceiveFifo::FlushEntries() {
    fifo_.Restore(geometry_.depth, fifo_.Moved());
    entries_.clear();
    drained_ = fifo_.Moved();
}

void Sa11xxDmaReceiveFifo::ClearFifo() {
    FlushEntries();
    fifo_.SetSupply(0u);
}

uint64_t Sa11xxDmaReceiveFifo::GateLimit(const int32_t* values, uint64_t n) const {
    if (geometry_.gate_mask == 0) return DmaBurstFifo::kUnlimited;
    const auto limit = [this](uint64_t at) -> uint64_t {
        if (at < geometry_.gate_window) return 0u;
        return ((at - geometry_.gate_window) / geometry_.burst + 1u) * geometry_.burst;
    };
    uint64_t at = 0;
    for (const int32_t v : entries_) {
        if (Blocks(v)) return limit(at);
        ++at;
    }
    for (uint64_t i = 0; i < n; ++i, ++at) {
        if (Blocks(values != nullptr ? values[i] : kNoValue)) return limit(at);
    }
    return DmaBurstFifo::kUnlimited;
}

bool Sa11xxDmaReceiveFifo::GateAllows(uint64_t words) const {
    return words <= fifo_.Moved() || words - fifo_.Moved() <= GateLimit(nullptr, 0u);
}

uint64_t Sa11xxDmaReceiveFifo::Move(uint64_t n, const int32_t* values) {
    const uint64_t limit  = GateLimit(values, n);
    const uint64_t supply = fifo_.Supply();
    const uint64_t moved  = fifo_.Moved();
    const bool     gated  = limit < supply;
    if (gated) fifo_.SetSupply(limit);
    uint64_t lost = 0u;
    if (n == 0u) {
        fifo_.Refill();
    } else {
        lost = fifo_.Take(n);
    }
    if (gated) fifo_.SetSupply(supply - (fifo_.Moved() - moved));
    return lost;
}

uint64_t Sa11xxDmaReceiveFifo::Store(uint64_t n, const int32_t* values) {
    if (n == 0u) return 0u;
    const uint64_t lost = Move(n, values);
    for (uint64_t i = 0; i < n - lost; ++i) {
        entries_.push_back(values != nullptr ? values[i] : kNoValue);
    }
    Drain();
    if (lost != 0u) {
        overrun_ = true;
        entries_.back() |= geometry_.overrun_tag;
    }
    return lost;
}

void Sa11xxDmaReceiveFifo::Drain() {
    while (drained_ < fifo_.Moved()) {
        if (entries_.empty()) {
            emu_.Get<Fatal>().Die("Sa11xxDmaReceiveFifo: the receive DMA moved word %llu that the "
                                  "FIFO never held", static_cast<unsigned long long>(drained_));
        }
        out_.push_back(entries_.front());
        entries_.pop_front();
        ++drained_;
    }
}

void Sa11xxDmaReceiveFifo::SetSupply(uint64_t words) {
    fifo_.SetSupply(words);
    Move(0u, nullptr);
    Drain();
}

bool Sa11xxDmaReceiveFifo::PopEntry(int32_t& value) {
    if (fifo_.Level() >= geometry_.depth) return false;
    if (entries_.empty()) {
        emu_.Get<Fatal>().Die("Sa11xxDmaReceiveFifo: a read of %u filled entries found no entry "
                              "held", Filled());
    }
    fifo_.Put();
    value = entries_.front();
    entries_.pop_front();
    if (geometry_.gate_mask != 0) {
        Move(0u, nullptr);
        Drain();
    }
    return true;
}

bool Sa11xxDmaReceiveFifo::Fill(uint32_t pa, uint32_t bytes) {
    const uint32_t datum = port_->DatumBytes();
    const uint32_t words = bytes / datum;
    if (out_.size() < words) {
        emu_.Get<Fatal>().Die("Sa11xxDmaReceiveFifo: the receive DMA wrote %u words while %zu were "
                              "moved", words, out_.size());
    }
    std::vector<uint8_t> data(bytes, 0u);
    bool valued = false;
    for (uint32_t i = 0; i < words; ++i) {
        const int32_t v = out_.front();
        out_.pop_front();
        if (v == kNoValue) continue;
        valued = true;
        cerf::le::PutN(&data[i * datum], static_cast<uint32_t>(v), datum);
    }
    if (!valued) return false;
    mem_->CopyIn(pa, data.data(), bytes);
    return true;
}

void Sa11xxDmaReceiveFifo::Save(StateWriter& w) const {
    w.Write<uint8_t>("dma_rx_overrun", overrun_ ? 1u : 0u);
    w.Write<uint32_t>("dma_rx_free", fifo_.Level());
    w.Write<uint64_t>("dma_rx_moved", fifo_.Moved());
    w.Write<uint64_t>("dma_rx_drained", drained_);
    w.Write<uint32_t>("dma_rx_entry_count", static_cast<uint32_t>(entries_.size()));
    for (int32_t v : entries_) w.Write("dma_rx_entry", v);
    w.Write<uint32_t>("dma_rx_out_count", static_cast<uint32_t>(out_.size()));
    for (int32_t v : out_) w.Write("dma_rx_out", v);
}

void Sa11xxDmaReceiveFifo::Restore(StateReader& r) {
    uint8_t  overrun = 0;
    uint32_t free = 0, n = 0;
    uint64_t moved = 0;
    r.Read("dma_rx_overrun", overrun);
    r.Read("dma_rx_free", free);
    r.Read("dma_rx_moved", moved);
    r.Read("dma_rx_drained", drained_);
    entries_.clear();
    r.Read("dma_rx_entry_count", n);
    for (uint32_t i = 0; i < n; ++i) {
        int32_t v = 0;
        r.Read("dma_rx_entry", v);
        entries_.push_back(v);
    }
    out_.clear();
    r.Read("dma_rx_out_count", n);
    for (uint32_t i = 0; i < n; ++i) {
        int32_t v = 0;
        r.Read("dma_rx_out", v);
        out_.push_back(v);
    }
    overrun_ = overrun != 0u;
    fifo_.Restore(free, moved);
    fifo_.SetSupply(0u);
}

}
