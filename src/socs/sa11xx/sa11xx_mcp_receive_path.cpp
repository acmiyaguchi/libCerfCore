#define NOMINMAX

#include "sa11xx_mcp_receive_path.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/state/state_stream.h>
#include "sa11xx_mcp_regs.h"

#include <algorithm>

using namespace sa11xx_mcp;

Sa11xxMcpReceivePath::Sa11xxMcpReceivePath(CerfEmulator& emu)
    : Sa11xxDmaReceiveFifo(emu, Geometry{kRxDepth, kRequestAt, kBurst}) {}

void Sa11xxMcpReceivePath::Reset() {
    armed_      = false;
    resume_     = false;
    first_conv_ = kNever;
    pushed_     = 0u;
    loop_armed_ = false;
    loop_.clear();
    schedule_.Close();
    ClearFifo();
}

void Sa11xxMcpReceivePath::Settle(uint64_t tick, bool counting) {
    if (looped_) {
        std::vector<int32_t> values;
        while (!loop_.empty() && loop_.front().tick <= tick) {
            values.push_back(loop_.front().value);
            loop_.pop_front();
        }
        Store(values.size(), values.data());
        pushed_ += values.size();
        return;
    }
    const uint64_t pushed = schedule_.PushesBy(tick);
    if (pushed > pushed_) {
        Store(pushed - pushed_, nullptr);
        pushed_ = pushed;
        armed_  = true;
    }
    schedule_.Advance(pushed_);
    if (schedule_.IsOpen() && !schedule_.Pending(pushed_) && !counting) schedule_.Close();
}

/* §11.12.5.1 (printed 11-141): "After MCDR0 is read, the bottom entry is invalidated"; §11.12.5.2
   (printed 11-142): "After MCDR1 is read, the bottom entry is invalidated". */
uint16_t Sa11xxMcpReceivePath::Pop() {
    int32_t v = kNoValue;
    if (!PopEntry(v)) return 0u;
    return v == kNoValue ? 0u : static_cast<uint16_t>(v);
}

/* §11.12.1.2 (printed 11-130): "a data valid bit is set any time a frame contains 'reliable' data
   ( the codec is enabled and at least one A-to-D sample has been taken)"; "once the data valid bit
   is set, it remains set until the codec A-to-D input is disabled". */
void Sa11xxMcpReceivePath::OnCodecWrite(bool adm, bool on, bool in, bool was_in, uint64_t carry,
                                        uint64_t period) {
    const uint64_t latch = carry + kSubframe0Ticks;
    const uint64_t edge  = carry + kFrameTicks;
    if (in && !was_in) first_conv_ = edge + period;
    else if (!in)      first_conv_ = kNever;
    /* §11.12.3.5 (printed 11-135): "When ADM=1, after the MCP is enabled, data is taken from the
       incoming frame when the data valid bit is set for the first time. After this point, the data
       valid bit is ignored". */
    if (!adm) {
        if (in && !was_in) {
            OpenEveryValidFrame(edge);
        } else if (!in && was_in && schedule_.IsOpen()) {
            schedule_.Limit(carry);
        }
    } else if (!schedule_.IsOpen() && ((in && !was_in) || (on && armed_))) {
        Open(edge, period, latch);
    } else if (on && schedule_.IsOpen()) {
        schedule_.Reload(edge, period);
    }
    if (!on && schedule_.IsOpen()) schedule_.Limit(carry);
}

void Sa11xxMcpReceivePath::Rephase(bool adm, uint64_t at, uint64_t old_period, uint64_t period,
                                   uint64_t tick) {
    if (adm && schedule_.IsOpen()) schedule_.Rephase(at, period);
    if (first_conv_ != kNever && first_conv_ - old_period > tick) {
        first_conv_ = first_conv_ - old_period + period;
    }
}

/* §11.12.3.5 (printed 11-135): with ADM=0 "data is taken from the incoming frame ... whenever the
   incoming audio or telecom data valid bit is set"; with ADM=1 samples "are stored each time the audio
   or telecom sample rate counters decrement to zero". */
void Sa11xxMcpReceivePath::SwitchMode(bool adm, uint64_t cut, uint64_t period, bool in, bool on,
                                      const Sa11xxMcpSampleCounter& counter) {
    if (!schedule_.IsOpen()) return;
    uint64_t zero = 0;
    if (!adm && in) {
        OpenEveryValidFrame(cut + kFrameTicks);
    } else if (adm && (in || (on && armed_)) && counter.NextZero(cut, period, zero)) {
        schedule_.Append(cut, zero - period, period, 1u);
    } else {
        schedule_.Limit(cut);
    }
}

/* §11.12.3.3 (printed 11-135): "the current frame, which is being transmitted, [is] terminated";
   "Clearing MCE resets the MCP's FIFOs". */
void Sa11xxMcpReceivePath::Freeze() {
    resume_ = schedule_.IsOpen();
    schedule_.Close();
    loop_.clear();
    pushed_ = 0u;
    ClearFifo();
}

/* §11.12.3.5 (printed 11-135): "When ADM=1, after the MCP is enabled, data is taken from the
   incoming frame when the data valid bit is set for the first time". */
void Sa11xxMcpReceivePath::Resume(uint64_t frozen, uint64_t base, bool adm, uint64_t period,
                                  const Sa11xxMcpSampleCounter& counter) {
    armed_      = false;
    loop_armed_ = false;
    if (first_conv_ != kNever && first_conv_ > frozen) first_conv_ += base - frozen;
    if (!resume_) return;
    resume_ = false;
    if (!looped_) Reopen(base, adm, period, counter);
}

void Sa11xxMcpReceivePath::Reopen(uint64_t edge, bool adm, uint64_t period,
                                  const Sa11xxMcpSampleCounter& counter) {
    if (first_conv_ == kNever) return;
    uint64_t zero = 0;
    if (!adm) {
        OpenEveryValidFrame(edge);
    } else if (counter.NextZero(edge, period, zero)) {
        Open(zero - period, period, edge);
    }
}

/* §11.12.3.10 (printed 11-137): "When LBM=1, the output of the serial shifter (MSB) is directly
   connected to the input of the serial shifter (LSB) internally and control of the TXD4, RXD4,
   SCLK, and SFRM pins are given to the peripheral pin control (PPC) unit." */
void Sa11xxMcpReceivePath::SetLooped(bool looped, uint64_t edge, bool adm, uint64_t period,
                                     const Sa11xxMcpSampleCounter& counter) {
    if (looped == looped_) return;
    looped_ = looped;
    loop_.clear();
    loop_armed_ = false;
    pushed_     = 0u;
    if (looped) {
        resume_ = false;
        schedule_.Close();
    } else {
        Reopen(edge, adm, period, counter);
    }
}

/* §11.12.1.1 (printed 11-128): the SFRM transfer sets "the appropriate audio/telecom valid flags";
   "the previous value transmitted is used"; §11.12.1.3 (printed 11-130): with ADM=1 "the audio data
   valid bit is ignored after the first conversion has been saved to the FIFO". */
void Sa11xxMcpReceivePath::LoopTakes(const std::vector<uint64_t>& zeros,
                                     const std::vector<uint16_t>& taken, uint16_t before,
                                     bool adm) {
    uint16_t value = before;
    for (size_t i = 0; i < zeros.size(); ++i) {
        if (i < taken.size()) {
            value       = taken[i];
            loop_armed_ = true;
        } else if (!adm || !loop_armed_) {
            continue;
        }
        loop_.push_back(LoopPush{Sa11xxMcpReceiveSchedule::NextFrameReturn(zeros[i]), value});
    }
}

bool Sa11xxMcpReceivePath::TickOfPush(uint64_t index, const LoopFuture& f, uint64_t& tick) const {
    if (!looped_) return schedule_.TickOfPush(index, tick);
    if (index <= pushed_) return false;
    const uint64_t j = index - pushed_;
    if (j <= loop_.size()) {
        tick = loop_[static_cast<size_t>(j - 1u)].tick;
        return true;
    }
    const uint64_t take = j - loop_.size();
    if (f.counter == nullptr) return false;
    if (!f.adm && take > f.moved_left) return false;
    if (f.adm && !loop_armed_ && f.moved_left == 0u) return false;
    uint64_t zero = 0;
    if (!f.counter->TakeTick(take, f.period, zero)) return false;
    tick = Sa11xxMcpReceiveSchedule::NextFrameReturn(zero);
    return true;
}

bool Sa11xxMcpReceivePath::TickOfMoved(uint64_t words, const LoopFuture& f, uint64_t& tick,
                                       bool& now) const {
    uint64_t m = 0;
    if (!TakesToMove(words, m)) return false;
    now = m == 0u;
    return now || TickOfPush(pushed_ + m, f, tick);
}

bool Sa11xxMcpReceivePath::NextRise(bool are, uint32_t request_at, const LoopFuture& f,
                                    uint64_t& tick) const {
    tick = kNever;
    uint64_t       at   = 0;
    const uint64_t left = TakesBeforeEmpty();
    const uint32_t lead = kRxDepth - request_at;
    if (are && left > lead && TickOfPush(pushed_ + left - lead, f, at)) tick = std::min(tick, at);
    if (!Overrun() && TickOfPush(pushed_ + left + 1u, f, at)) tick = std::min(tick, at);
    return tick != kNever;
}

void Sa11xxMcpReceivePath::Open(uint64_t origin, uint64_t period, uint64_t from) {
    schedule_.Open(origin, period, armed_ ? from : std::max(from, first_conv_));
    pushed_ = 0u;
}

void Sa11xxMcpReceivePath::OpenEveryValidFrame(uint64_t from) {
    if (!schedule_.IsOpen()) pushed_ = 0u;
    const uint64_t frame = std::max((from + kFrameTicks - 1u) / kFrameTicks * kFrameTicks,
                                    (first_conv_ / kFrameTicks + 1u) * kFrameTicks);
    schedule_.OpenEveryFrame(frame);
}

void Sa11xxMcpReceivePath::Save(StateWriter& w) const {
    Sa11xxDmaReceiveFifo::Save(w);
    w.Write<uint8_t>("mcp_rx_armed", armed_ ? 1u : 0u);
    w.Write<uint8_t>("mcp_rx_resume", resume_ ? 1u : 0u);
    w.Write<uint8_t>("mcp_rx_looped", looped_ ? 1u : 0u);
    w.Write<uint8_t>("mcp_rx_loop_armed", loop_armed_ ? 1u : 0u);
    w.Write<uint64_t>("mcp_rx_first_conv", first_conv_);
    w.Write<uint64_t>("mcp_rx_pushed", pushed_);
    w.Write<uint32_t>("mcp_rx_loop_count", static_cast<uint32_t>(loop_.size()));
    for (const LoopPush& p : loop_) {
        w.Write("mcp_rx_loop_tick", p.tick);
        w.Write("mcp_rx_loop_value", p.value);
    }
    schedule_.Save(w);
}

void Sa11xxMcpReceivePath::Restore(StateReader& r) {
    Sa11xxDmaReceiveFifo::Restore(r);
    uint8_t  armed = 0, resume = 0, looped = 0, loop_armed = 0;
    uint32_t n = 0;
    r.Read("mcp_rx_armed", armed);
    r.Read("mcp_rx_resume", resume);
    r.Read("mcp_rx_looped", looped);
    r.Read("mcp_rx_loop_armed", loop_armed);
    r.Read("mcp_rx_first_conv", first_conv_);
    r.Read("mcp_rx_pushed", pushed_);
    loop_.clear();
    r.Read("mcp_rx_loop_count", n);
    for (uint32_t i = 0; i < n; ++i) {
        LoopPush p;
        r.Read("mcp_rx_loop_tick", p.tick);
        r.Read("mcp_rx_loop_value", p.value);
        loop_.push_back(p);
    }
    schedule_.Restore(r);
    armed_      = armed != 0u;
    resume_     = resume != 0u;
    looped_     = looped != 0u;
    loop_armed_ = loop_armed != 0u;
}
