#include "freescale_uart_transmitter.h"

#include <cerfcore/state/state_stream.h>

#include <algorithm>

namespace cerfcore {

using namespace cerf_freescale_uart;

void FreescaleUartTransmitter::Reset() {
    fifo_.clear();
    busy_       = false;
    data_done_  = false;
    ready_      = kNever;
    line_free_  = 0u;
    mark_until_ = 0u;
    brk_from_   = kNever;
    brk_end_    = kNever;
    brk_seen_   = false;
    txfe_       = true;
    burst_      = false;
}

/* MCIMX51RM Table 59-9 TXEN: "If TXEN is negated in the middle of a transmission, the UART disables
   the transmitter immediately, and starts marking 1s"; Table 59-8 SNDBRK: "The transmitter finishes
   sending the character in progress (if any) and sends BREAK characters until SNDBRK is reset". */
void FreescaleUartTransmitter::Configure(uint64_t tick, bool enabled, bool hold, bool sndbrk,
                                         bool frcperr, const FreescaleUartFrame& frame,
                                         std::vector<FreescaleUartTxEvent>& out) {
    frame_   = frame;
    frcperr_ = frcperr;
    hold_    = hold;
    if (enabled_ && !enabled) {
        if (busy_) {
            busy_      = false;
            data_done_ = false;
            line_free_ = tick;
        }
        if (fifo_.empty()) txfe_ = true;
        if (brk_from_ != kNever) EndBreak(tick, out);
        ready_ = kNever;
    }
    if (enabled && sndbrk && !(enabled_ && sndbrk_) && !busy_ && brk_from_ == kNever) {
        brk_from_ = tick;
        brk_seen_ = false;
    }
    if (sndbrk) brk_end_ = kNever;
    if (sndbrk_ && !sndbrk && brk_from_ != kNever) brk_end_ = BreakEndAt(tick);
    enabled_ = enabled;
    sndbrk_  = sndbrk;
    if (!CanStart()) ready_ = kNever;
    Ready(tick);
}

/* MCIMX51RM section 59.4.4.2 (page 59-42): "Following completion of the BREAK transmission, the UART
   will transmit two mark bits." */
void FreescaleUartTransmitter::EndBreak(uint64_t tick, std::vector<FreescaleUartTxEvent>& out) {
    out.push_back({FreescaleUartTxEvent::Kind::kBreakEnd, 0u, false, tick});
    line_free_  = tick;
    mark_until_ = tick + 2u * kBitTicks;
    brk_from_   = kNever;
    brk_end_    = kNever;
    brk_seen_   = false;
}

/* MCIMX51RM section 59.4.4.2 (page 59-42): "The transmitter samples SNDBRK after every bit is
   transmitted." */
uint64_t FreescaleUartTransmitter::BreakEndAt(uint64_t tick) const {
    const uint64_t bits = std::max<uint64_t>(1u, (tick - brk_from_ + kBitTicks - 1u) / kBitTicks);
    return brk_from_ + bits * kBitTicks;
}

/* MCIMX51RM section 59.4.5 (page 59-43): the receiver "samples the following data bits at the
   bit-center"; section 59.4.3 BREAK: "all of the data bits, including the stop bit, are logic 0". */
uint64_t FreescaleUartTransmitter::BreakSampled() const {
    if (brk_from_ == kNever || brk_seen_) return kNever;
    return brk_from_ + uint64_t{kBitTicks} * (frame_.bits - 1u) + kBitTicks / 2u;
}

void FreescaleUartTransmitter::Ready(uint64_t tick) {
    if (busy_ || !CanStart() || ready_ != kNever) return;
    ready_ = std::max({tick, line_free_, mark_until_});
}

/* MCIMX51RM section 59.4.4 (page 59-41): "The data is written consecutively if the TxFIFO is not
   full"; section 59.4.4.1: "Writing data to the TxFIFO would release the interrupt flag". */
void FreescaleUartTransmitter::Write(uint64_t tick, uint8_t byte) {
    if (fifo_.size() >= kFifoDepth) return;
    fifo_.push_back(byte);
    txfe_ = false;
    if (fifo_.size() >= 2u) burst_ = true;
    Ready(tick);
}

uint64_t FreescaleUartTransmitter::NextTick() const {
    if (!busy_) return std::min({ready_, BreakSampled(), brk_end_});
    return data_done_ ? FrameEnd() : DataEnd();
}

/* MCIMX51RM section 59.4.4.1 (page 59-41) and Figure 59-20 (page 59-42): TXFE asserts when the last
   character leaves a FIFO that held two or more, or when the shift register empties after a single
   character, "before the transmission of the parity bit (if exists) and the stop bit(s)". */
void FreescaleUartTransmitter::Advance(uint64_t tick, std::vector<FreescaleUartTxEvent>& out) {
    for (uint64_t next = NextTick(); next <= tick; next = NextTick()) {
        if (!busy_ && next == brk_end_) {
            EndBreak(next, out);
            Ready(next);
        } else if (!busy_ && next == BreakSampled()) {
            brk_seen_ = true;
            out.push_back({FreescaleUartTxEvent::Kind::kBreak, 0u, false, brk_from_});
        } else if (!busy_) {
            Start(next, out);
        } else if (!data_done_) {
            data_done_ = true;
            if (fifo_.empty()) {
                txfe_  = true;
                burst_ = false;
            }
        } else {
            Finish(next, out);
        }
    }
}

void FreescaleUartTransmitter::Start(uint64_t tick, std::vector<FreescaleUartTxEvent>& out) {
    char_      = fifo_.front();
    fifo_.pop_front();
    busy_      = true;
    data_done_ = false;
    start_     = tick;
    ready_     = kNever;
    char_data_ = frame_.data_bits;
    char_bits_ = frame_.bits;
    char_perr_ = frcperr_ && frame_.parity;
    if (fifo_.empty() && burst_) {
        txfe_  = true;
        burst_ = false;
    }
    out.push_back({FreescaleUartTxEvent::Kind::kStarted, char_, char_perr_, tick});
}

/* MCIMX51RM Table 59-7 TX_DATA: "In 7-bit mode, D7 is ignored". */
void FreescaleUartTransmitter::Finish(uint64_t tick, std::vector<FreescaleUartTxEvent>& out) {
    const uint8_t byte = char_data_ == 7u ? static_cast<uint8_t>(char_ & 0x7Fu) : char_;
    out.push_back({FreescaleUartTxEvent::Kind::kFinished, byte, char_perr_, tick});
    busy_      = false;
    data_done_ = false;
    line_free_ = tick;
    if (sndbrk_ && enabled_) {
        brk_from_ = tick;
        brk_seen_ = false;
    }
    Ready(tick);
}

void FreescaleUartTransmitter::Save(StateWriter& w) const {
    const std::vector<uint8_t> fifo(fifo_.begin(), fifo_.end());
    w.Write<uint32_t>("tx_fifo_count", static_cast<uint32_t>(fifo.size()));
    w.WriteBytes("tx_fifo", fifo.data(), fifo.size());
    w.Write("tx_frame_data", frame_.data_bits);
    w.Write("tx_frame_bits", frame_.bits);
    w.Write<uint8_t>("tx_frame_parity", frame_.parity ? 1u : 0u);
    w.Write<uint8_t>("tx_frame_odd", frame_.odd ? 1u : 0u);
    w.Write<uint8_t>("tx_enabled", enabled_ ? 1u : 0u);
    w.Write<uint8_t>("tx_hold", hold_ ? 1u : 0u);
    w.Write<uint8_t>("tx_sndbrk", sndbrk_ ? 1u : 0u);
    w.Write<uint8_t>("tx_frcperr", frcperr_ ? 1u : 0u);
    w.Write<uint8_t>("tx_busy", busy_ ? 1u : 0u);
    w.Write<uint8_t>("tx_data_done", data_done_ ? 1u : 0u);
    w.Write("tx_char", char_);
    w.Write<uint8_t>("tx_char_perr", char_perr_ ? 1u : 0u);
    w.Write("tx_char_data", char_data_);
    w.Write("tx_char_bits", char_bits_);
    w.Write("tx_start", start_);
    w.Write("tx_ready", ready_);
    w.Write("tx_line_free", line_free_);
    w.Write("tx_mark_until", mark_until_);
    w.Write("tx_brk_from", brk_from_);
    w.Write("tx_brk_end", brk_end_);
    w.Write<uint8_t>("tx_brk_seen", brk_seen_ ? 1u : 0u);
    w.Write<uint8_t>("tx_txfe", txfe_ ? 1u : 0u);
    w.Write<uint8_t>("tx_burst", burst_ ? 1u : 0u);
}

void FreescaleUartTransmitter::Restore(StateReader& r) {
    uint32_t count = 0;
    r.Read("tx_fifo_count", count);
    std::vector<uint8_t> fifo(count);
    r.ReadBytes("tx_fifo", fifo.data(), fifo.size());
    fifo_.assign(fifo.begin(), fifo.end());
    uint8_t parity = 0, odd = 0, enabled = 0, hold = 0, sndbrk = 0, frcperr = 0, busy = 0, done = 0;
    uint8_t perr = 0, txfe = 0, burst = 0, brk_seen = 0;
    r.Read("tx_frame_data", frame_.data_bits);
    r.Read("tx_frame_bits", frame_.bits);
    r.Read("tx_frame_parity", parity);
    r.Read("tx_frame_odd", odd);
    r.Read("tx_enabled", enabled);
    r.Read("tx_hold", hold);
    r.Read("tx_sndbrk", sndbrk);
    r.Read("tx_frcperr", frcperr);
    r.Read("tx_busy", busy);
    r.Read("tx_data_done", done);
    r.Read("tx_char", char_);
    r.Read("tx_char_perr", perr);
    r.Read("tx_char_data", char_data_);
    r.Read("tx_char_bits", char_bits_);
    r.Read("tx_start", start_);
    r.Read("tx_ready", ready_);
    r.Read("tx_line_free", line_free_);
    r.Read("tx_mark_until", mark_until_);
    r.Read("tx_brk_from", brk_from_);
    r.Read("tx_brk_end", brk_end_);
    r.Read("tx_brk_seen", brk_seen);
    r.Read("tx_txfe", txfe);
    r.Read("tx_burst", burst);
    frame_.parity = parity != 0u;
    frame_.odd    = odd != 0u;
    enabled_      = enabled != 0u;
    hold_         = hold != 0u;
    sndbrk_       = sndbrk != 0u;
    frcperr_      = frcperr != 0u;
    busy_         = busy != 0u;
    data_done_    = done != 0u;
    char_perr_    = perr != 0u;
    txfe_         = txfe != 0u;
    burst_        = burst != 0u;
    brk_seen_     = brk_seen != 0u;
}

}
