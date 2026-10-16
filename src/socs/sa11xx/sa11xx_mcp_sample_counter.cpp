#include "sa11xx_mcp_sample_counter.h"

#include <cerfcore/state/state_stream.h>

#include <algorithm>

namespace cerfcore {

void Sa11xxMcpSampleCounter::Reset() {
    counting_ = false;
    stop_     = kNever;
    next_     = kNever;
}

void Sa11xxMcpSampleCounter::Enable(uint64_t origin) {
    counting_ = true;
    origin_   = origin;
    stop_     = kNever;
    next_     = kNever;
    taken_    = 0u;
}

/* SA-1110 Developer's Manual §11.12.1.2 (printed 11-129): the counters "are reloaded with the
   programmed modulus value any time the audio portion of the codec is enabled (which is also
   accomplished by performing a control register write transfer)". */
void Sa11xxMcpSampleCounter::Reload(uint64_t edge) {
    stop_ = edge;
    next_ = edge;
}

void Sa11xxMcpSampleCounter::Shift(uint64_t at, uint64_t by) {
    if (counting_ && origin_ <= at) origin_ += by;
}

uint64_t Sa11xxMcpSampleCounter::Rephase(uint64_t period) {
    origin_ += taken_ * period;
    taken_   = 0u;
    return origin_;
}

/* §11.12.1.3: at the enable "the next available entry of data is taken from the audio transmit
   FIFO"; "After the audio D-to-A conversion is made ... This reload triggers the audio transmit
   FIFO to transfer the next available entry". */
uint64_t Sa11xxMcpSampleCounter::Settle(uint64_t tick, uint64_t period) {
    uint64_t takes = 0u;
    while (counting_) {
        const uint64_t last = stop_ == kNever ? tick : std::min(tick, stop_ - 1u);
        if (last >= origin_) {
            const uint64_t due = (last - origin_) / period + 1u;
            if (due > taken_) {
                takes += due - taken_;
                taken_ = due;
            }
        }
        if (stop_ == kNever || tick < stop_) break;
        if (next_ == kNever) {
            counting_ = false;
            stop_     = kNever;
            break;
        }
        origin_ = next_;
        next_   = kNever;
        stop_   = kNever;
        taken_  = 1u;
    }
    return takes;
}

uint64_t Sa11xxMcpSampleCounter::TakesLeftInRun(uint64_t period) const {
    if (stop_ == kNever) return kNever;
    if (stop_ <= origin_) return 0u;
    const uint64_t total = (stop_ - 1u - origin_) / period + 1u;
    return total > taken_ ? total - taken_ : 0u;
}

bool Sa11xxMcpSampleCounter::TakeTick(uint64_t take, uint64_t period, uint64_t& tick) const {
    if (!counting_ || take == 0u) return false;
    const uint64_t left = TakesLeftInRun(period);
    if (left == kNever || take <= left) {
        tick = origin_ + (taken_ + take - 1u) * period;
        return true;
    }
    if (next_ == kNever) return false;
    tick = next_ + (take - left) * period;
    return true;
}

/* §11.12.1.2 (printed 11-129): "Samples/conversions are made each time the audio codec's counter
   reaches zero"; Figure 11-31: a divisor of 12 reaches zero after "384 (32*12=384) SCLK cycles". */
bool Sa11xxMcpSampleCounter::NextZero(uint64_t at, uint64_t period, uint64_t& zero) const {
    if (!counting_) return false;
    const uint64_t k = at <= origin_ + period ? 1u : (at - origin_ + period - 1u) / period;
    zero = origin_ + k * period;
    if (stop_ == kNever || zero < stop_) return true;
    if (next_ == kNever) return false;
    zero = next_ + period * (at <= next_ + period ? 1u : (at - next_ + period - 1u) / period);
    return true;
}

void Sa11xxMcpSampleCounter::Save(StateWriter& w) const {
    w.Write<uint8_t>("counter_counting", counting_ ? 1u : 0u);
    w.Write<uint64_t>("counter_origin", origin_);
    w.Write<uint64_t>("counter_stop", stop_);
    w.Write<uint64_t>("counter_next", next_);
    w.Write<uint64_t>("counter_taken", taken_);
}

void Sa11xxMcpSampleCounter::Restore(StateReader& r) {
    uint8_t counting = 0;
    r.Read("counter_counting", counting);
    r.Read("counter_origin", origin_);
    r.Read("counter_stop", stop_);
    r.Read("counter_next", next_);
    r.Read("counter_taken", taken_);
    counting_ = counting != 0u;
}

}
