#pragma once

#include "freescale_uart_regs.h"

#include <cstdint>
#include <deque>
#include <vector>

namespace cerfcore {

class StateReader;
class StateWriter;

struct FreescaleUartTxEvent {
    enum class Kind : uint8_t { kStarted, kFinished, kBreak, kBreakEnd };
    Kind     kind;
    uint8_t  byte;
    bool     parity_error;
    uint64_t tick;
};

class FreescaleUartTransmitter {
public:
    static constexpr uint64_t kNever = UINT64_MAX;

    void Reset();
    void Configure(uint64_t tick, bool enabled, bool hold, bool sndbrk, bool frcperr,
                   const cerf_freescale_uart::FreescaleUartFrame& frame,
                   std::vector<FreescaleUartTxEvent>& out);
    void Write(uint64_t tick, uint8_t byte);
    void Advance(uint64_t tick, std::vector<FreescaleUartTxEvent>& out);

    uint64_t NextTick() const;
    uint32_t Level() const { return static_cast<uint32_t>(fifo_.size()); }
    bool     Txfe() const { return txfe_; }
    bool     Txdc() const { return fifo_.empty() && !busy_; }

    void Save(StateWriter& w) const;
    void Restore(StateReader& r);

private:
    bool CanStart() const {
        return enabled_ && !hold_ && !sndbrk_ && brk_from_ == kNever && !fifo_.empty();
    }
    void Ready(uint64_t tick);
    void Start(uint64_t tick, std::vector<FreescaleUartTxEvent>& out);
    void Finish(uint64_t tick, std::vector<FreescaleUartTxEvent>& out);
    void EndBreak(uint64_t tick, std::vector<FreescaleUartTxEvent>& out);
    uint64_t BreakEndAt(uint64_t tick) const;
    uint64_t DataEnd() const { return start_ + cerf_freescale_uart::kBitTicks * (1u + char_data_); }
    uint64_t FrameEnd() const { return start_ + cerf_freescale_uart::kBitTicks * char_bits_; }
    uint64_t BreakSampled() const;

    std::deque<uint8_t> fifo_;
    cerf_freescale_uart::FreescaleUartFrame frame_;
    bool     enabled_    = false;
    bool     hold_       = false;
    bool     sndbrk_     = false;
    bool     frcperr_    = false;
    bool     busy_       = false;
    bool     data_done_  = false;
    uint8_t  char_       = 0;
    bool     char_perr_  = false;
    uint32_t char_data_  = 8;
    uint32_t char_bits_  = 10;
    uint64_t start_      = 0;
    uint64_t ready_      = kNever;
    uint64_t line_free_  = 0;
    uint64_t mark_until_ = 0;
    uint64_t brk_from_   = kNever;
    uint64_t brk_end_    = kNever;
    bool     brk_seen_   = false;
    bool     txfe_       = true;
    bool     burst_      = false;
};

}
