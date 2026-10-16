#pragma once

#include <cerfcore/core/service.h>
#include "../dma_burst_fifo.h"

#include <cstddef>
#include <cstdint>
#include <deque>

namespace cerfcore {

class EmulatedMemory;
class Sa11xxDmaPort;
class StateReader;
class StateWriter;

class Sa11xxDmaReceiveFifo : public Service {
public:
    static constexpr int32_t kNoValue = -1;

    bool ShouldRegister() override;
    void OnReady() override;

    void Attach(const Sa11xxDmaPort* port) { port_ = port; }
    void FlushEntries();
    void ClearFifo();
    void ClearOverrun() { overrun_ = false; }

    uint64_t Store(uint64_t n, const int32_t* values);
    void     SetSupply(uint64_t words);
    bool     PopEntry(int32_t& value);
    bool     Fill(uint32_t pa, uint32_t bytes);

    uint32_t Filled() const { return geometry_.depth - fifo_.Level(); }
    int32_t  FrontEntry() const { return entries_.front(); }
    uint64_t Moved() const { return fifo_.Moved(); }
    bool     Supplied() const { return fifo_.Supply() != 0u; }
    bool     Overrun() const { return overrun_; }
    uint64_t TakesBeforeEmpty() const { return fifo_.TakesBeforeEmpty(); }

    bool RequestBlocked() const {
        if (geometry_.gate_mask == 0) return false;
        const size_t window = geometry_.gate_window;
        const size_t n      = entries_.size() < window ? entries_.size() : window;
        for (size_t i = 0; i < n; ++i) {
            if (Blocks(entries_[i])) return true;
        }
        return false;
    }

    bool TakesToMove(uint64_t words, uint64_t& n) const {
        if (geometry_.gate_mask != 0 && !GateAllows(words)) return false;
        return fifo_.TakesToMove(words, n);
    }

    void Save(StateWriter& w) const;
    void Restore(StateReader& r);

protected:
    struct Geometry {
        uint32_t depth       = 0;
        uint32_t request_at  = 0;
        uint32_t burst       = 0;
        uint32_t gate_window = 0;
        int32_t  gate_mask   = 0;
        int32_t  overrun_tag = 0;
    };

    Sa11xxDmaReceiveFifo(CerfEmulator& emu, Geometry geometry)
        : Service(emu), geometry_(geometry) {}

private:
    bool Blocks(int32_t entry) const {
        return entry != kNoValue && (entry & geometry_.gate_mask) != 0;
    }

    uint64_t GateLimit(const int32_t* values, uint64_t n) const;
    bool     GateAllows(uint64_t words) const;
    uint64_t Move(uint64_t n, const int32_t* values);
    void     Drain();

    const Geometry       geometry_;
    EmulatedMemory*      mem_  = nullptr;
    const Sa11xxDmaPort* port_ = nullptr;
    DmaBurstFifo         fifo_;
    std::deque<int32_t>  entries_;
    std::deque<int32_t>  out_;
    uint64_t             drained_ = 0;
    bool                 overrun_ = false;
};

}
