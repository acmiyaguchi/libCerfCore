#include "sa11xx_mcp_receive_schedule.h"

#include <cerfcore/state/state_stream.h>
#include "sa11xx_mcp_regs.h"

#include <algorithm>

using sa11xx_mcp::kFrameTicks;
using sa11xx_mcp::kSubframe0Ticks;

uint64_t Sa11xxMcpReceiveSchedule::Count(const Segment& s) {
    if (s.k_hi == kNever) return kNever;
    return s.k_hi >= s.k_lo ? s.k_hi - s.k_lo + 1u : 0u;
}

/* §11.12.1.2 Figure 11-31: the counter starts on the SFRM after the enable and reaches zero
   after 32 x divisor SCLK; §11.12.1.3: at that zero "a sample and A-to-D conversion is made and
   the converted value is placed ... for transmission back to the MCP in the next data frame". */
void Sa11xxMcpReceiveSchedule::Open(uint64_t origin, uint64_t period, uint64_t first_valid) {
    open_ = true;
    base_ = 0u;
    Segment s;
    s.origin = origin;
    s.period = period;
    s.k_lo   = first_valid <= origin + period ? 1u : (first_valid - origin + period - 1u) / period;
    segments_.assign(1u, s);
}

/* SA-1110 Developer's Manual §11.12.3 MCCR0 ADM (printed 11-138): "0 - Audio and telecom receive
   data is stored to their respective FIFOs whenever their receive data valid bits are valid";
   UCB1200 datasheet p.38: "By default (after reset), the data valid bits will be continuously '1'". */
void Sa11xxMcpReceiveSchedule::OpenEveryFrame(uint64_t first_frame) {
    const uint64_t origin = first_frame - kFrameTicks;
    if (!open_) {
        open_ = true;
        base_ = 0u;
        segments_.assign(1u, Segment{origin, kFrameTicks, 0u, kNever});
        return;
    }
    Append(origin, origin, kFrameTicks, 0u);
}

void Sa11xxMcpReceiveSchedule::Append(uint64_t limit, uint64_t origin, uint64_t period, uint64_t k_lo) {
    Limit(limit);
    segments_.push_back(Segment{origin, period, k_lo, kNever});
}

void Sa11xxMcpReceiveSchedule::Limit(uint64_t limit) {
    Segment& s = segments_.back();
    if (limit <= s.origin) {
        s.k_lo = 1u;
        s.k_hi = 0u;
        return;
    }
    s.k_hi = std::min(s.k_hi, (limit - 1u - s.origin) / s.period);
}

/* §11.12.1.2 (printed 11-129): the counters "are reloaded with the programmed modulus value any
   time the audio portion of the codec is enabled (which is also accomplished by performing a
   control register write transfer)". */
void Sa11xxMcpReceiveSchedule::Reload(uint64_t origin, uint64_t period) {
    Append(origin, origin, period, 1u);
}

void Sa11xxMcpReceiveSchedule::Rephase(uint64_t at, uint64_t period) {
    for (size_t i = 0; i < segments_.size(); ++i) {
        Segment& s = segments_[i];
        if (s.origin >= at) {
            s.period = period;
            continue;
        }
        if (i + 1u < segments_.size() && segments_[i + 1u].origin < at) continue;
        const uint64_t keep = (at - s.origin) / s.period;
        if (s.k_hi != kNever && s.k_hi <= keep) continue;
        Segment n;
        n.origin = at;
        n.period = period;
        n.k_lo   = s.k_lo <= keep + 1u ? 1u
                 : std::max<uint64_t>(1u, (s.origin + s.k_lo * s.period - at + period - 1u) / period);
        n.k_hi   = s.k_hi == kNever ? kNever : (s.origin + s.k_hi * s.period - at) / period;
        s.k_hi   = keep;
        segments_.insert(segments_.begin() + static_cast<std::ptrdiff_t>(i) + 1, n);
        ++i;
    }
}

void Sa11xxMcpReceiveSchedule::Advance(uint64_t pushed) {
    while (segments_.size() > 1u) {
        const uint64_t c = Count(segments_.front());
        if (pushed - base_ < c) return;
        base_ += c;
        segments_.erase(segments_.begin());
    }
}

bool Sa11xxMcpReceiveSchedule::Pending(uint64_t pushed) const {
    if (!open_) return false;
    const uint64_t c = Count(segments_.front());
    return segments_.size() > 1u || c == kNever || pushed - base_ < c;
}

/* SA-1110 §11.12.1.1 (printed 11-128): "The SFRM pulse causes the MCP to transfer any available audio
   and/or telecom data from their respective transmit FIFOs to a 64-bit serial shifter"; "After 64 SCLK
   cycles elapse ... The MCP takes the data from each field". */
uint64_t Sa11xxMcpReceiveSchedule::NextFrameReturn(uint64_t tick) {
    return (tick / kFrameTicks + 1u) * kFrameTicks + kSubframe0Ticks;
}

/* SA-1110 §11.12.3.5 (printed 11-135): with ADM=1 the sample of a counter zero "will be available
   in the next frame"; UCB1200 Fig.33 (printed p.38): the sample latched at fsa passes a DFF clocked
   at bit 21 and loads into the shift register at bit 0. */
uint64_t Sa11xxMcpReceiveSchedule::PushTick(const Segment& s, uint64_t k) {
    return NextFrameReturn(s.origin + k * s.period);
}

uint64_t Sa11xxMcpReceiveSchedule::SegmentPushesBy(const Segment& s, uint64_t tick) {
    if (tick < kSubframe0Ticks) return 0u;
    const uint64_t frames_started = (tick - kSubframe0Ticks) / kFrameTicks * kFrameTicks;
    if (frames_started <= s.origin) return 0u;
    const uint64_t k = std::min(s.k_hi, (frames_started - 1u - s.origin) / s.period);
    return k >= s.k_lo ? k - s.k_lo + 1u : 0u;
}

uint64_t Sa11xxMcpReceiveSchedule::PushesBy(uint64_t tick) const {
    if (!open_) return 0u;
    uint64_t pushes = base_;
    for (const Segment& s : segments_) pushes += SegmentPushesBy(s, tick);
    return pushes;
}

bool Sa11xxMcpReceiveSchedule::TickOfPush(uint64_t index, uint64_t& tick) const {
    if (!open_ || index <= base_) return false;
    uint64_t i = index - base_;
    for (const Segment& s : segments_) {
        const uint64_t c = Count(s);
        if (c == kNever || i <= c) {
            tick = PushTick(s, s.k_lo + i - 1u);
            return true;
        }
        i -= c;
    }
    return false;
}

void Sa11xxMcpReceiveSchedule::Save(StateWriter& w) const {
    w.Write<uint8_t>("rx_open", open_ ? 1u : 0u);
    w.Write<uint64_t>("rx_base", base_);
    w.Write<uint32_t>("rx_segment_count", static_cast<uint32_t>(segments_.size()));
    for (const Segment& s : segments_) {
        w.Write("rx_segment_origin", s.origin);
        w.Write("rx_segment_period", s.period);
        w.Write("rx_segment_k_lo", s.k_lo);
        w.Write("rx_segment_k_hi", s.k_hi);
    }
}

void Sa11xxMcpReceiveSchedule::Restore(StateReader& r) {
    uint8_t  open = 0;
    uint32_t n    = 0;
    r.Read("rx_open", open);
    r.Read("rx_base", base_);
    r.Read("rx_segment_count", n);
    segments_.assign(n, Segment{});
    for (Segment& s : segments_) {
        r.Read("rx_segment_origin", s.origin);
        r.Read("rx_segment_period", s.period);
        r.Read("rx_segment_k_lo", s.k_lo);
        r.Read("rx_segment_k_hi", s.k_hi);
    }
    open_ = open != 0u;
}
