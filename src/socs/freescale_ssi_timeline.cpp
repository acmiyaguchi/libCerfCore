#include "freescale_ssi_timeline.h"

#include <cerfcore/state/state_stream.h>

#include <algorithm>
#include <string>

namespace cerfcore {

void FreescaleSsiTimeline::Reset() {
    data_    = 0u;
    network_ = two_ch_ = enabled_ = run_ = false;
    first_ = last_slot_ = frame_done_ = false;
    settled_ = ord_ = 0u;
    start_ = stop_ = restart_ = done_at_ = kNever;
    grid_.Stop();
}

void FreescaleSsiTimeline::SetRoute(uint32_t data, bool network, bool two_channel) {
    data_    = data;
    network_ = network;
    two_ch_  = two_channel;
}

void FreescaleSsiTimeline::Restart() {
    settled_ = ord_ = 0u;
    start_   = run_ && grid_.Running() ? grid_.AcceptedFrame(0u) : kNever;
    stop_ = restart_ = done_at_ = kNever;
    first_   = false;
}

void FreescaleSsiTimeline::Enable(const FreescaleSsiFrameShape& shape, bool run) {
    enabled_   = true;
    run_       = run;
    last_slot_ = frame_done_ = false;
    grid_.Stop();
    grid_.Apply(shape);
    Restart();
}

/* MCIMX31RM Table 45-9 SSIEN (page 45-34): "When disabled, all SSI status bits are preset to the
   same state produced by the power-on reset". */
void FreescaleSsiTimeline::Disable(bool run) {
    enabled_ = false;
    run_     = run;
    grid_.Stop();
    first_ = last_slot_ = frame_done_ = false;
    settled_ = ord_ = 0u;
    start_ = stop_ = restart_ = done_at_ = kNever;
}

bool FreescaleSsiTimeline::Reshape(const FreescaleSsiFrameShape& shape) {
    if (!grid_.Apply(shape)) return false;
    Restart();
    return true;
}

/* MCIMX31RM Table 45-9 (page 45-33 / 45-34): RE "data reception starts with the arrival of the next
   frame sync", TE "the transmitter continues to send data until the end of the current frame and
   then stops", "set again before the second to last bit ... data transmission continues". */
void FreescaleSsiTimeline::SetRun(bool on) {
    run_ = on;
    if (!Running()) return;
    const uint64_t cur = settled_ + lag_;
    if (on) {
        frame_done_ = false;
        if (start_ != kNever && stop_ != kNever && grid_.NowBit() + 2u >= grid_.SlotBit(stop_)) {
            restart_ = grid_.AcceptedFrame(stop_);
            return;
        }
        done_at_ = kNever;
        if (start_ != kNever) {
            stop_ = kNever;
            return;
        }
        start_ = grid_.AcceptedFrame(grid_.NextFrame(cur));
        stop_  = kNever;
        ord_   = 0u;
        return;
    }
    if (restart_ != kNever) {
        restart_ = kNever;
        return;
    }
    if (start_ == kNever) return;
    /* MCIMX51RM Table 56-13 TFRC (page 56-42): "set at the end of the frame during which
       Transmitter is disabled". */
    if (start_ >= cur) {
        if (grid_.AcceptedFrame(start_) == start_) {
            done_at_ = start_;
            start_   = kNever;
        } else {
            stop_    = start_ + grid_.Slots();
            done_at_ = stop_;
        }
        return;
    }
    stop_    = grid_.AcceptedFrame(grid_.NextFrame(cur));
    done_at_ = stop_;
}

/* MCIMX51RM Table 56-13 TLS (page 56-44): "TLS is not generated when frame rate is 1 in normal
   mode of operation". */
bool FreescaleSsiTimeline::LastSlotGenerated() const {
    return grid_.Slots() > 1u || network_;
}

uint64_t FreescaleSsiTimeline::DataSlots(uint64_t from, uint64_t to) const {
    if (start_ == kNever) return 0u;
    const uint64_t lo = std::max(from, start_);
    const uint64_t hi = std::min(to, stop_);
    return hi > lo ? grid_.DataSlotsBefore(data_, hi) - grid_.DataSlotsBefore(data_, lo) : 0u;
}

/* MCIMX31RM §45.1.2.2 (page 45-9): "The first data word is taken from FIFO 0 and transmitted in the
   first enabled time slot and subsequently, data is loaded from FIFO 1 and FIFO 0 alternately". */
uint64_t FreescaleSsiTimeline::Uses(uint32_t k, uint64_t ord, uint64_t n) const {
    if (!two_ch_) return k == 0u ? n : 0u;
    if (k == 0u) return (ord + n + 1u) / 2u - (ord + 1u) / 2u;
    return (ord + n) / 2u - ord / 2u;
}

uint64_t FreescaleSsiTimeline::NthUseIn(uint32_t k, uint64_t m, uint64_t from, uint64_t ord) const {
    const uint64_t skip    = two_ch_ && ord % 2u != k ? 1u : 0u;
    const uint64_t ordinal = two_ch_ ? skip + 2u * (m - 1u) : m - 1u;
    return grid_.NthDataSlot(data_, from, ordinal + 1u);
}

uint64_t FreescaleSsiTimeline::NthUse(uint32_t k, uint64_t m) const {
    if (start_ == kNever || (!two_ch_ && k != 0u)) return kNever;
    const uint64_t from = std::max(settled_, start_);
    if (stop_ != kNever) {
        const uint64_t left = Uses(k, ord_, DataSlots(from, stop_));
        if (m > left) return restart_ == kNever ? kNever : NthUseIn(k, m - left, restart_, 0u);
    }
    return NthUseIn(k, m, from, ord_);
}

uint64_t FreescaleSsiTimeline::CycleOfEvent(uint64_t slot) {
    return grid_.CycleOfSlot(slot + lag_);
}

uint64_t FreescaleSsiTimeline::NextFrameStart() const {
    return grid_.Running() ? grid_.NextFrame(settled_ + lag_) : 0u;
}

bool FreescaleSsiTimeline::FrameEnded(uint64_t from, uint64_t to) const {
    const uint64_t frame = grid_.SlotAt(from, 0u, start_, stop_);
    return frame != kNever && to > frame + grid_.Slots();
}

uint64_t FreescaleSsiTimeline::NextSlot(uint64_t from, uint32_t pos) const {
    const uint64_t s = grid_.SlotAt(from, pos, start_, stop_);
    if (s != kNever || restart_ == kNever) return s;
    return grid_.SlotAt(from, pos, restart_, kNever);
}

/* MCIMX51RM Table 56-13 TFS (page 56-44): "In Normal mode, this bit is high for the first time
   slot." */
uint64_t FreescaleSsiTimeline::FirstFall() const {
    const uint64_t next = settled_ + lag_;
    if (NextSlot(next, 0u) != next) return next;
    return stop_ == kNever || restart_ == stop_ ? kNever : stop_;
}

void FreescaleSsiTimeline::FinishStop() {
    start_   = restart_;
    stop_    = kNever;
    restart_ = kNever;
    ord_     = 0u;
}

uint64_t FreescaleSsiTimeline::EventsEnd() {
    const uint64_t c = grid_.Started();
    return c > lag_ ? c - lag_ : 0u;
}

bool FreescaleSsiTimeline::Next(uint64_t end, Span& span) {
    if (stop_ != kNever && settled_ >= stop_) FinishStop();
    if (settled_ >= end) return false;
    const uint64_t to   = stop_ != kNever ? std::min(end, stop_) : end;
    const uint64_t last = grid_.SlotAt(settled_, grid_.Slots() - 1u, start_, stop_);
    if (LastSlotGenerated() && last != kNever && last < to) last_slot_ = true;
    if (lag_ == 0u) first_ = grid_.SlotAt(to - 1u, 0u, start_, stop_) == to - 1u;
    span.n   = DataSlots(settled_, to);
    span.ord = ord_;
    span.to  = to;
    ord_    += span.n;
    settled_ = to;
    return true;
}

void FreescaleSsiTimeline::Finish() {
    const uint64_t c = grid_.Started();
    if (lag_ != 0u) first_ = c != 0u && NextSlot(c - 1u, 0u) == c - 1u;
    if (done_at_ != kNever && c > done_at_) {
        frame_done_ = true;
        done_at_    = kNever;
    }
}

uint64_t FreescaleSsiTimeline::NextStatusCycle(bool first, bool last, bool done) {
    if (!Running()) return kNever;
    uint64_t at = kNever;
    if (first) {
        const uint64_t slot = first_ ? FirstFall() : NextSlot(settled_ + lag_, 0u);
        if (slot != kNever) at = std::min(at, grid_.CycleOfSlot(slot));
    }
    if (last && !last_slot_ && LastSlotGenerated()) {
        const uint64_t slot = NextSlot(settled_, grid_.Slots() - 1u);
        if (slot != kNever) at = std::min(at, CycleOfEvent(slot));
    }
    if (done && done_at_ != kNever) at = std::min(at, grid_.CycleOfSlot(done_at_));
    return at;
}

void FreescaleSsiTimeline::Save(StateWriter& w, const char* prefix) {
    const std::string p(prefix);
    w.Write<uint8_t>((p + "enabled").c_str(), enabled_ ? 1u : 0u);
    w.Write<uint8_t>((p + "run").c_str(), run_ ? 1u : 0u);
    w.Write<uint8_t>((p + "first").c_str(), first_ ? 1u : 0u);
    w.Write<uint8_t>((p + "last_slot").c_str(), last_slot_ ? 1u : 0u);
    w.Write<uint8_t>((p + "frame_done").c_str(), frame_done_ ? 1u : 0u);
    w.Write<uint8_t>((p + "network").c_str(), network_ ? 1u : 0u);
    w.Write<uint8_t>((p + "two_ch").c_str(), two_ch_ ? 1u : 0u);
    w.Write<uint32_t>((p + "data").c_str(), data_);
    w.Write<uint64_t>((p + "settled").c_str(), settled_);
    w.Write<uint64_t>((p + "ord").c_str(), ord_);
    w.Write<uint64_t>((p + "start").c_str(), start_);
    w.Write<uint64_t>((p + "stop").c_str(), stop_);
    w.Write<uint64_t>((p + "restart").c_str(), restart_);
    w.Write<uint64_t>((p + "done_at").c_str(), done_at_);
    grid_.Save(w, prefix);
}

void FreescaleSsiTimeline::Restore(StateReader& r, const char* prefix) {
    const std::string p(prefix);
    uint8_t enabled = 0, run = 0, first = 0, last_slot = 0, frame_done = 0, network = 0, two_ch = 0;
    r.Read((p + "enabled").c_str(), enabled);
    r.Read((p + "run").c_str(), run);
    r.Read((p + "first").c_str(), first);
    r.Read((p + "last_slot").c_str(), last_slot);
    r.Read((p + "frame_done").c_str(), frame_done);
    r.Read((p + "network").c_str(), network);
    r.Read((p + "two_ch").c_str(), two_ch);
    r.Read((p + "data").c_str(), data_);
    r.Read((p + "settled").c_str(), settled_);
    r.Read((p + "ord").c_str(), ord_);
    r.Read((p + "start").c_str(), start_);
    r.Read((p + "stop").c_str(), stop_);
    r.Read((p + "restart").c_str(), restart_);
    r.Read((p + "done_at").c_str(), done_at_);
    grid_.Restore(r, prefix);
    enabled_    = enabled != 0u;
    run_        = run != 0u;
    first_      = first != 0u;
    last_slot_  = last_slot != 0u;
    frame_done_ = frame_done != 0u;
    network_    = network != 0u;
    two_ch_     = two_ch != 0u;
}

}
