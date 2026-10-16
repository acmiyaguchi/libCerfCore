#include "freescale_uart_receiver.h"

#include <cerfcore/state/state_stream.h>

#include <algorithm>
#include <vector>

namespace cerfcore {

using namespace cerf_freescale_uart;

void FreescaleUartReceiver::Reset(bool drop_line) {
    fifo_.clear();
    if (busy_) cur_lost_ = true;
    if (drop_line) {
        wire_.clear();
        busy_      = false;
        line_free_ = 0u;
        line_low_  = false;
    }
    counting_  = false;
    anchor_    = 0u;
    idle_from_ = kNever;
    idle_wait_ = false;
    agtim_ = idle_ = wake_ = awake_ = airint_ = irint_ = ore_ = brcd_ = framerr_ = parityerr_ = false;
}

/* MCIMX51RM Table 59-21 LOOP: "the receiver input is internally connected to the transmitter and
   ignores the RXD pin". */
void FreescaleUartReceiver::Configure(uint64_t tick, const FreescaleUartRxConfig& cfg) {
    if (cfg.loop != loop_) {
        if (busy_) cur_lost_ = true;
        if (cfg.loop) wire_.clear();
        line_low_ = false;
    }
    frame_      = cfg.frame;
    rxtl_       = cfg.rxtl == 0u ? 1u : cfg.rxtl;
    icd_frames_ = cfg.icd_frames;
    iden_       = cfg.iden;
    enabled_    = cfg.enabled;
    loop_       = cfg.loop;
    hold_       = cfg.hold;
    ir_         = cfg.ir;
    invr_       = cfg.invr;
    Counting(tick);
}

void FreescaleUartReceiver::Inject(uint64_t tick, const uint8_t* data, size_t n) {
    if (loop_) return;
    if (wire_.empty() && !busy_) wire_from_ = tick;
    for (size_t i = 0; i < n; ++i) wire_.push_back(Char{data[i], 0u});
}

void FreescaleUartReceiver::LoopChar(uint64_t start, uint8_t byte, bool parity_error, Events& out) {
    if (loop_) Begin(start, Char{byte, parity_error ? kParity : uint8_t{0}}, false, out);
}

void FreescaleUartReceiver::LoopBreak(uint64_t start, Events& out) {
    if (!loop_) return;
    Begin(start, Char{0u, kBreak}, false, out);
    line_low_ = true;
}

void FreescaleUartReceiver::LoopBreakEnd(uint64_t tick) {
    if (!loop_ || !line_low_) return;
    line_low_ = false;
    anchor_   = tick;
    if (!busy_) idle_from_ = tick;
}

/* MCIMX51RM section 59.4.5.3 (page 59-46): "The generation of the asynchronous AWAKE interrupt does
   not need any clocks"; "The generation of the asynchronous AIRINT interrupt does not need any clocks." */
void FreescaleUartReceiver::EdgeWithoutClock() {
    if (busy_ || loop_ || wire_.empty() || (wire_.front().flags & kEdgeSeen) != 0u) return;
    wire_.front().flags |= kEdgeSeen;
    if (ir_) airint_ = true;
    else     awake_  = true;
}

/* MCIMX51RM section 59.4.5.2 (page 59-45): the ageing counter "is reset when either a RxFIFO read is
   performed or another character starts to present on the RXD line"; Table 59-13 AWAKE: "a falling
   edge was detected on the RXD Serial pin". */
void FreescaleUartReceiver::Begin(uint64_t start, const Char& c, bool pin, Events& out) {
    cur_       = c;
    busy_      = true;
    start_     = start;
    stage_     = Stage::kWake;
    cur_bits_  = frame_.bits;
    cur_data_  = frame_.data_bits;
    cur_lost_  = false;
    cur_pin_   = pin;
    anchor_    = start;
    idle_from_ = kNever;
    idle_wait_ = false;
    const uint32_t bits =
        WireBits(c.byte, (c.flags & kParity) != 0u, (c.flags & kBreak) != 0u, frame_);
    uint32_t i = 1u;
    while (i < cur_bits_ && (bits & (1u << i)) == 0u) ++i;
    rise_ = i < cur_bits_ ? start + uint64_t{kBitTicks} * i : kNever;
    while (i < cur_bits_ && (bits & (1u << i)) != 0u) ++i;
    fall_ = i < cur_bits_ ? start + uint64_t{kBitTicks} * i : kNever;
    /* MCIMX51RM section 59.4.5.3 (page 59-46): "When IR interface is enabled (UCR1[7]=1), the AWAKE
       bit is always not asserted"; section 59.4.7.4 (page 59-56): INVR 0 asserts IRINT on a falling
       edge of the RXD pin, INVR 1 on a rising edge. */
    if (pin) {
        if ((c.flags & kEdgeSeen) == 0u) {
            if (ir_) airint_ = true;
            else     awake_  = true;
        }
        if (ir_ && !invr_) irint_ = true;
    }
    out.push_back({FreescaleUartRxEvent::Kind::kStart, start, 0u});
}

void FreescaleUartReceiver::SkipAbsentStages() {
    if (stage_ == Stage::kRise && rise_ == kNever) stage_ = Stage::kFall;
    if (stage_ == Stage::kFall && fall_ == kNever) stage_ = Stage::kStore;
}

/* MCIMX51RM section 59.4.5 (page 59-43): the receiver "samples the following data bits at the
   bit-center"; section 59.4.5.3 (page 59-45): for WAKE "the RXD line must stays at low level for
   more than a half-bit duration". */
uint64_t FreescaleUartReceiver::StageTick() const {
    switch (stage_) {
        case Stage::kWake:  return start_ + kBitTicks / 2u;
        case Stage::kRise:  return rise_;
        case Stage::kFall:  return fall_;
        case Stage::kStore: return start_ + uint64_t{kBitTicks} * (cur_bits_ - 1u) + kBitTicks / 2u;
        default:            return start_ + uint64_t{kBitTicks} * cur_bits_;
    }
}

/* MCIMX51RM section 59.4.8.1 (page 59-59): in the Doze State with DOZE set, the UART "will complete
   the receive/transmit of the current character and signal to the far-end transmitter/receiver to
   stop sending/receiving". */
uint64_t FreescaleUartReceiver::NextStart() const {
    if (busy_ || loop_ || hold_ || wire_.empty() || fifo_.size() >= kFifoDepth) return kNever;
    return std::max(line_free_, wire_from_);
}

/* MCIMX51RM Table 59-13 AGTIM: "idle for a time of 8 character lengths (where a character length
   consists of 7 or 8 bits, depending on the setting of the WS bit in UCR2". */
uint64_t FreescaleUartReceiver::AgingDue() const {
    if (!counting_ || agtim_ || line_low_) return kNever;
    return anchor_ + 8u * uint64_t{frame_.data_bits} * kBitTicks;
}

/* MCIMX51RM section 59.4.5.1 (page 59-44): "RxFIFO must be empty" and "RXD pin must be idle for
   more than a configured number of frames (ICD[1:0] = UCR1[11:10])"; Table 59-25 (page 59-45):
   IDEN 0 gives IDLE 0. */
uint64_t FreescaleUartReceiver::IdleDue() const {
    if (!iden_ || idle_from_ == kNever || idle_ || line_low_) return kNever;
    return idle_from_ + uint64_t{icd_frames_} * kBitTicks * frame_.bits;
}

uint64_t FreescaleUartReceiver::NextTick() const {
    const uint64_t line = busy_ ? StageTick() : NextStart();
    return std::min({line, AgingDue(), IdleDue()});
}

void FreescaleUartReceiver::Advance(uint64_t tick, Events& out) {
    using Kind = FreescaleUartRxEvent::Kind;
    for (uint64_t next = NextTick(); next <= tick; next = NextTick()) {
        if (busy_ && next == StageTick()) {
            switch (stage_) {
                case Stage::kWake:
                    wake_  = true;
                    stage_ = Stage::kRise;
                    break;
                case Stage::kRise:
                    if (cur_pin_ && ir_ && invr_) irint_ = true;
                    out.push_back({Kind::kRise, next, 0u});
                    stage_ = Stage::kFall;
                    break;
                case Stage::kFall:
                    out.push_back({Kind::kFall, next, 0u});
                    stage_ = Stage::kStore;
                    break;
                case Stage::kStore:
                    Store(next, out);
                    stage_ = Stage::kEnd;
                    break;
                default:
                    busy_      = false;
                    line_free_ = next;
                    if (!line_low_) idle_from_ = next;
                    break;
            }
            SkipAbsentStages();
        } else if (!busy_ && next == NextStart()) {
            const Char c = wire_.front();
            wire_.pop_front();
            Begin(next, c, true, out);
        } else if (next == AgingDue()) {
            agtim_ = true;
        } else {
            idle_from_ = kNever;
            if (fifo_.empty()) idle_ = true;
            else               idle_wait_ = true;
        }
    }
}

/* MCIMX51RM Figure 59-21 (page 59-44): RX FIFO full sets ORE; "Already 31 chars in RX-FIFO" sets ERR
   and OVRRUN; Table 59-6 BRK: "When odd parity is selected, PRERR is also set when BRK is set". */
void FreescaleUartReceiver::Store(uint64_t tick, Events& out) {
    if (cur_lost_ || !enabled_ || (cur_pin_ && ir_ && invr_)) return;
    uint16_t entry = cur_data_ == 7u ? static_cast<uint16_t>(cur_.byte & 0x7Fu) : cur_.byte;
    if ((cur_.flags & kBreak) != 0u) {
        entry    = static_cast<uint16_t>(kUrxdBrk | kUrxdFrmerr);
        brcd_    = true;
        framerr_ = true;
        if (frame_.parity && frame_.odd) {
            entry |= kUrxdPrerr;
            parityerr_ = true;
        }
    }
    if ((cur_.flags & kParity) != 0u) {
        entry |= kUrxdPrerr;
        parityerr_ = true;
    }
    if (fifo_.size() >= kFifoDepth) {
        ore_ = true;
        return;
    }
    if (fifo_.size() == kFifoDepth - 1u) entry |= kUrxdOvrrun;
    if ((entry & (kUrxdOvrrun | kUrxdFrmerr | kUrxdBrk | kUrxdPrerr)) != 0u) entry |= kUrxdErr;
    fifo_.push_back(entry);
    Counting(tick);
    out.push_back({FreescaleUartRxEvent::Kind::kStored, tick, entry});
}

/* MCIMX51RM section 59.4.5.2 (page 59-45): the ageing timer "starts to count as soon as the RxFIFO
   is not empty and its trigger level is not reached (RRDY=0)". */
void FreescaleUartReceiver::Counting(uint64_t tick) {
    const bool c = !fifo_.empty() && Level() < rxtl_;
    if (c && !counting_) anchor_ = tick;
    counting_ = c;
}

void FreescaleUartReceiver::Emptied(uint64_t tick) {
    if (!wire_.empty() && !busy_) wire_from_ = std::max(wire_from_, tick);
    anchor_ = tick;
    Counting(tick);
    if (fifo_.empty() && idle_wait_) {
        idle_      = true;
        idle_wait_ = false;
    }
}

uint16_t FreescaleUartReceiver::Pop(uint64_t tick) {
    if (fifo_.empty()) return 0u;
    const uint16_t entry = fifo_.front();
    fifo_.pop_front();
    Emptied(tick);
    return static_cast<uint16_t>(entry | kUrxdCharrdy);
}

uint32_t FreescaleUartReceiver::Take(uint64_t tick, uint32_t max, uint8_t* out) {
    uint32_t n = 0;
    for (; n < max && !fifo_.empty(); ++n) {
        out[n] = static_cast<uint8_t>(fifo_.front());
        fifo_.pop_front();
    }
    if (n != 0u) Emptied(tick);
    return n;
}

void FreescaleUartReceiver::ClearFlags(uint64_t tick, uint32_t usr1, uint32_t usr2) {
    if ((usr1 & kUsr1Parityerr) != 0u) parityerr_ = false;
    if ((usr1 & kUsr1Framerr) != 0u)   framerr_   = false;
    if ((usr1 & kUsr1Awake) != 0u)     awake_     = false;
    if ((usr1 & kUsr1Airint) != 0u)    airint_    = false;
    if ((usr1 & kUsr1Agtim) != 0u && agtim_) {
        agtim_  = false;
        anchor_ = tick;
    }
    if ((usr2 & kUsr2Idle) != 0u)  idle_  = false;
    if ((usr2 & kUsr2Wake) != 0u)  wake_  = false;
    if ((usr2 & kUsr2Irint) != 0u) irint_ = false;
    if ((usr2 & kUsr2Brcd) != 0u)  brcd_  = false;
    if ((usr2 & kUsr2Ore) != 0u)   ore_   = false;
}

/* MCIMX51RM Table 59-13 RXDS: "RXDS is active only when the receiver is enabled". */
uint32_t FreescaleUartReceiver::Usr1() const {
    uint32_t v = 0u;
    if (parityerr_)              v |= kUsr1Parityerr;
    if (framerr_)                v |= kUsr1Framerr;
    if (Level() >= rxtl_)        v |= kUsr1Rrdy;
    if (agtim_)                  v |= kUsr1Agtim;
    if (enabled_ && !busy_ && !line_low_) v |= kUsr1Rxds;
    if (airint_)                 v |= kUsr1Airint;
    if (awake_)                  v |= kUsr1Awake;
    return v;
}

uint32_t FreescaleUartReceiver::Usr2() const {
    uint32_t v = 0u;
    if (idle_)          v |= kUsr2Idle;
    if (irint_)         v |= kUsr2Irint;
    if (wake_)          v |= kUsr2Wake;
    if (brcd_)          v |= kUsr2Brcd;
    if (ore_)           v |= kUsr2Ore;
    if (!fifo_.empty()) v |= kUsr2Rdr;
    return v;
}

void FreescaleUartReceiver::Save(StateWriter& w) const {
    std::vector<uint8_t> wire;
    for (const Char& c : wire_) { wire.push_back(c.byte); wire.push_back(c.flags); }
    const std::vector<uint16_t> fifo(fifo_.begin(), fifo_.end());
    w.Write<uint32_t>("rx_wire_count", static_cast<uint32_t>(wire_.size()));
    w.WriteBytes("rx_wire", wire.data(), wire.size());
    w.Write<uint32_t>("rx_fifo_count", static_cast<uint32_t>(fifo.size()));
    w.WriteBytes("rx_fifo", fifo.data(), fifo.size() * sizeof(uint16_t));
    w.Write("rx_frame_data", frame_.data_bits);
    w.Write("rx_frame_bits", frame_.bits);
    w.Write<uint8_t>("rx_frame_parity", frame_.parity ? 1u : 0u);
    w.Write<uint8_t>("rx_frame_odd", frame_.odd ? 1u : 0u);
    const uint8_t flags[] = {enabled_, loop_, busy_, cur_lost_, line_low_, counting_, idle_wait_,
                             agtim_, idle_, wake_, awake_, ore_, brcd_, framerr_, parityerr_};
    w.WriteBytes("rx_flags", flags, sizeof(flags));
    const uint8_t ir_flags[] = {hold_, ir_, invr_, cur_pin_, airint_, irint_};
    w.WriteBytes("rx_ir_flags", ir_flags, sizeof(ir_flags));
    w.Write("rx_rxtl", rxtl_);
    w.Write("rx_icd_frames", icd_frames_);
    w.Write<uint8_t>("rx_iden", iden_ ? 1u : 0u);
    w.Write("rx_cur_byte", cur_.byte);
    w.Write("rx_cur_flags", cur_.flags);
    w.Write<uint8_t>("rx_char_stage", static_cast<uint8_t>(stage_));
    w.Write("rx_start", start_);
    w.Write("rx_rise", rise_);
    w.Write("rx_fall", fall_);
    w.Write("rx_cur_bits", cur_bits_);
    w.Write("rx_cur_data", cur_data_);
    w.Write("rx_wire_from", wire_from_);
    w.Write("rx_line_free", line_free_);
    w.Write("rx_anchor", anchor_);
    w.Write("rx_idle_from", idle_from_);
}

void FreescaleUartReceiver::Restore(StateReader& r) {
    uint32_t wire_count = 0, fifo_count = 0;
    r.Read("rx_wire_count", wire_count);
    std::vector<uint8_t> wire(wire_count * 2u);
    r.ReadBytes("rx_wire", wire.data(), wire.size());
    wire_.clear();
    for (uint32_t i = 0; i < wire_count; ++i) wire_.push_back(Char{wire[2u * i], wire[2u * i + 1u]});
    r.Read("rx_fifo_count", fifo_count);
    std::vector<uint16_t> fifo(fifo_count);
    r.ReadBytes("rx_fifo", fifo.data(), fifo.size() * sizeof(uint16_t));
    fifo_.assign(fifo.begin(), fifo.end());
    uint8_t parity = 0, odd = 0, stage = 0;
    r.Read("rx_frame_data", frame_.data_bits);
    r.Read("rx_frame_bits", frame_.bits);
    r.Read("rx_frame_parity", parity);
    r.Read("rx_frame_odd", odd);
    frame_.parity = parity != 0u;
    frame_.odd    = odd != 0u;
    uint8_t flags[15] = {};
    r.ReadBytes("rx_flags", flags, sizeof(flags));
    bool* const dst[] = {&enabled_, &loop_, &busy_, &cur_lost_, &line_low_, &counting_, &idle_wait_,
                         &agtim_, &idle_, &wake_, &awake_, &ore_, &brcd_, &framerr_, &parityerr_};
    for (size_t i = 0; i < sizeof(flags); ++i) *dst[i] = flags[i] != 0u;
    uint8_t ir_flags[6] = {};
    r.ReadBytes("rx_ir_flags", ir_flags, sizeof(ir_flags));
    bool* const ir_dst[] = {&hold_, &ir_, &invr_, &cur_pin_, &airint_, &irint_};
    for (size_t i = 0; i < sizeof(ir_flags); ++i) *ir_dst[i] = ir_flags[i] != 0u;
    r.Read("rx_rxtl", rxtl_);
    r.Read("rx_icd_frames", icd_frames_);
    uint8_t iden = 0;
    r.Read("rx_iden", iden);
    iden_ = iden != 0u;
    r.Read("rx_cur_byte", cur_.byte);
    r.Read("rx_cur_flags", cur_.flags);
    r.Read("rx_char_stage", stage);
    stage_ = static_cast<Stage>(stage);
    r.Read("rx_start", start_);
    r.Read("rx_rise", rise_);
    r.Read("rx_fall", fall_);
    r.Read("rx_cur_bits", cur_bits_);
    r.Read("rx_cur_data", cur_data_);
    r.Read("rx_wire_from", wire_from_);
    r.Read("rx_line_free", line_free_);
    r.Read("rx_anchor", anchor_);
    r.Read("rx_idle_from", idle_from_);
}

}
