#include "wm97xx_digitiser.h"

#include <cerfcore/state/state_stream.h>

#include <algorithm>
#include <iterator>

void Wm97xxDigitiser::Sequence::Add(uint8_t tag, uint32_t frames) {
    tags[count]  = tag;
    steps[count] = frames;
    ++count;
}

bool Wm97xxDigitiser::Sequence::operator==(const Sequence& o) const {
    if (period != o.period || count != o.count || group != o.group || pen_gated != o.pen_gated ||
        slot != o.slot || hold != o.hold) {
        return false;
    }
    return std::equal(tags, tags + count, o.tags) && std::equal(steps, steps + count, o.steps);
}

uint64_t Wm97xxDigitiser::SetSpan(const Burst& b, uint32_t set) {
    uint64_t span = 0u;
    for (uint32_t p = 0u; p < b.seq.group; ++p) span += b.seq.steps[set * b.seq.group + p];
    return span;
}

/* Wolfson WM9705 page 46 (Table 26): the conversion rate "is the MAXIMUM that may be obtained ... if
   the number of conversions to be performed, and the delay applied by the DEL[3-0] bits to each
   conversion, add up to a time greater than the nominal period". */
uint64_t Wm97xxDigitiser::SetPeriod(const Burst& b, uint32_t set) {
    const uint64_t span = SetSpan(b, set);
    return b.polled ? span : std::max<uint64_t>(b.seq.period, span);
}

uint64_t Wm97xxDigitiser::SetOffset(const Burst& b, uint32_t set) {
    uint64_t off = 0u;
    for (uint32_t s = 0u; s < set; ++s) off += SetPeriod(b, s);
    return off;
}

uint64_t Wm97xxDigitiser::CycleFrames(const Burst& b) { return SetOffset(b, SetsPerCycle(b)); }

/* Wolfson WM9705 Figure 22 (page 45): a conversion requested in frame w runs DELAY from frame w + 1
   and CONVERT in the frame after, and its result is available from the next frame. */
uint64_t Wm97xxDigitiser::ConversionOffset(const Burst& b, uint32_t i) {
    const uint32_t set = i / b.seq.group;
    uint64_t       off = SetOffset(b, set);
    for (uint32_t p = set * b.seq.group; p <= i; ++p) off += b.seq.steps[p];
    return off;
}

uint64_t Wm97xxDigitiser::DoneInCycle(const Burst& b, uint64_t frames) {
    uint64_t done = 0u, base = 0u;
    for (uint32_t s = 0u; s < SetsPerCycle(b); ++s) {
        uint64_t at = base;
        for (uint32_t p = 0u; p < b.seq.group; ++p) {
            at += b.seq.steps[s * b.seq.group + p];
            if (at > frames) return done;
            ++done;
        }
        base += SetPeriod(b, s);
    }
    return done;
}

uint64_t Wm97xxDigitiser::Pos(const Burst& b, uint64_t i) {
    return (i / b.seq.count) * CycleFrames(b) + ConversionOffset(b, static_cast<uint32_t>(i % b.seq.count));
}

uint64_t Wm97xxDigitiser::ResultFrame(const Burst& b, uint64_t k) { return b.start + Pos(b, b.first + k); }

uint64_t Wm97xxDigitiser::Conversions(const Burst& b, uint64_t frames, bool capped) {
    const uint64_t limit = b.cut == kOpen ? frames : std::min(frames, b.cut + 1u);
    if (limit <= b.start) return 0u;
    const uint64_t x      = limit - 1u - b.start;
    const uint64_t cycle  = CycleFrames(b);
    const uint64_t cycles = cycle == 0u ? 0u : x / cycle;
    const uint64_t all    = cycles * b.seq.count + DoneInCycle(b, x - cycles * cycle);
    uint64_t       done   = all > b.first ? all - b.first : 0u;
    if (b.sets != kOpen) done = std::min(done, b.sets * b.seq.group - b.first);
    return capped ? std::min(done, b.cap) : done;
}

uint64_t Wm97xxDigitiser::SetsCheckedBefore(const Burst& b, uint64_t frame) {
    if (frame <= b.start) return 0u;
    const uint64_t x      = frame - 1u - b.start;
    const uint64_t cycle  = CycleFrames(b);
    const uint64_t cycles = x / cycle;
    const uint64_t rem    = x - cycles * cycle;
    uint64_t checked = cycles * SetsPerCycle(b);
    for (uint32_t s = 0u; s < SetsPerCycle(b) && SetOffset(b, s) + SetSpan(b, s) <= rem; ++s) ++checked;
    return checked;
}

uint64_t Wm97xxDigitiser::Total(const Burst& b, bool capped) {
    uint64_t total = kOpen;
    if (b.cut != kOpen) {
        total = Conversions(b, b.cut + 1u, false);
    } else if (b.sets != kOpen) {
        total = b.sets * b.seq.group - b.first;
    }
    return capped ? std::min(total, b.cap) : total;
}

uint64_t Wm97xxDigitiser::LastFrame(const Burst& b) {
    const uint64_t total = Total(b);
    if (total == kOpen) return kOpen;
    return total == 0u ? b.start : ResultFrame(b, total - 1u);
}

void Wm97xxDigitiser::Clear(bool pen_down) {
    ClearHistory(pen_down);
    continuous_ = false;
    config_     = Sequence{};
}

void Wm97xxDigitiser::ClearHistory(bool pen_down) {
    bursts_.clear();
    edges_.clear();
    pen0_ = pen_down;
    std::fill(std::begin(pruned_slots_), std::end(pruned_slots_), 0u);
    std::fill(std::begin(frontier_), std::end(frontier_), 0u);
    prune_mask_     = 0u;
    pruned_results_ = 0u;
    read_           = 0u;
    has_pruned_     = false;
    pruned_last_    = Result{};
}

void Wm97xxDigitiser::CloseLast(uint64_t frame) {
    if (bursts_.empty()) return;
    Burst& b = bursts_.back();
    if (LastFrame(b) > frame) b.cut = std::min(b.cut, frame);
}

uint64_t Wm97xxDigitiser::ResultsBefore(uint64_t frames) const {
    uint64_t n = pruned_results_;
    for (const Burst& b : bursts_) n += Conversions(b, frames);
    return n;
}

/* Wolfson WM9705 page 43: with WAIT set "the current conversion result may not be written into the results
   register, until the previous conversion result has been read. The sequence will halt at the finish of
   this current conversion". */
void Wm97xxDigitiser::OpenBurst(uint64_t start, const Sequence& seq, bool polled) {
    Burst b;
    b.seq    = seq;
    b.start  = start;
    b.sets   = polled ? 1u : kOpen;
    b.cut    = kOpen;
    b.polled = polled;
    if (seq.hold) b.cap = ResultsBefore(start) > read_ ? 0u : 1u;
    bursts_.push_back(b);
}

void Wm97xxDigitiser::ReadResult(uint64_t frame) {
    read_ = ResultsBefore(frame + 1u);
    if (bursts_.empty()) return;
    Burst& b = bursts_.back();
    if (!b.seq.hold) return;
    const uint64_t written = Conversions(b, frame + 1u);
    if (written < b.cap || written >= Total(b, false)) return;
    const uint64_t ready = ResultFrame(b, written);
    if (ready > frame) {
        b.cap = written + 1u;
        return;
    }
    Burst c = b;
    c.start += frame + 1u - ready;
    c.first += written;
    c.cap = 1u;
    bursts_.push_back(c);
}

void Wm97xxDigitiser::SetContinuous(uint64_t frame, const Sequence* seq) {
    if (seq != nullptr && continuous_ && *seq == config_) return;
    CloseLast(frame);
    continuous_ = seq != nullptr;
    if (!continuous_) return;
    config_ = *seq;
    if (!config_.pen_gated || PenDownAt(frame + 1u)) OpenBurst(frame + 1u, config_, false);
}

void Wm97xxDigitiser::StartPolled(uint64_t frame, const Sequence& seq) {
    CloseLast(frame);
    continuous_ = false;
    Sequence at_write = seq;
    std::fill(std::begin(at_write.steps), std::end(at_write.steps), 0u);
    OpenBurst(frame, at_write, true);
}

/* Wolfson WM9705 page 46: "Pen-down detection is performed after completion of each set of
   conversions"; with PDEN set "and a pen-down is NOT detected, then the conversion process will come
   to a stop, until the pen is once more returned to the screen". */
void Wm97xxDigitiser::SetPen(uint64_t frame, bool down) {
    if (PenDownAt(frame) == down) return;
    edges_.push_back(Edge{frame, down});
    if (!continuous_ || !config_.pen_gated) return;
    Burst* last = bursts_.empty() ? nullptr : &bursts_.back();
    const bool ours = last != nullptr && !last->polled && last->cut == kOpen;
    if (!down) {
        if (!ours || last->sets != kOpen) return;
        last->sets = 1u + SetsCheckedBefore(*last, frame);
        return;
    }
    if (ours && (last->sets == kOpen || LastFrame(*last) >= frame)) {
        last->sets = kOpen;
        return;
    }
    OpenBurst(frame, config_, false);
}

bool Wm97xxDigitiser::PenDownAt(uint64_t frame) const {
    bool down = pen0_;
    for (const Edge& e : edges_) {
        if (e.frame > frame) break;
        down = e.down;
    }
    return down;
}

bool Wm97xxDigitiser::Polling(uint64_t frame) const {
    if (bursts_.empty()) return false;
    const Burst& b = bursts_.back();
    if (!b.polled || b.cut != kOpen) return false;
    return Total(b) < Total(b, false) || frame < LastFrame(b);
}

Wm97xxDigitiser::Result Wm97xxDigitiser::ResultOf(const Burst& b, uint64_t k) const {
    Result r;
    r.tag      = b.seq.tags[(b.first + k) % b.seq.count];
    r.pen_down = PenDownAt(ResultFrame(b, k));
    return r;
}

uint64_t Wm97xxDigitiser::SlotWordsBefore(uint32_t slot, uint64_t frames) const {
    uint64_t n = pruned_slots_[slot];
    for (const Burst& b : bursts_) {
        if (b.seq.slot == slot) n += Conversions(b, frames);
    }
    return n;
}

bool Wm97xxDigitiser::FrameOfSlotWord(uint32_t slot, uint64_t n, uint64_t& frame) const {
    if (n <= pruned_slots_[slot]) return false;
    uint64_t base = pruned_slots_[slot];
    for (const Burst& b : bursts_) {
        if (b.seq.slot != slot) continue;
        const uint64_t total = Total(b);
        if (total == kOpen || n <= base + total) {
            frame = ResultFrame(b, n - base - 1u);
            return true;
        }
        base += total;
    }
    return false;
}

bool Wm97xxDigitiser::SlotWord(uint32_t slot, uint64_t n, Result& r) const {
    if (n <= pruned_slots_[slot]) return false;
    uint64_t base = pruned_slots_[slot];
    for (const Burst& b : bursts_) {
        if (b.seq.slot != slot) continue;
        const uint64_t total = Total(b);
        if (total == kOpen || n <= base + total) {
            r = ResultOf(b, n - base - 1u);
            return true;
        }
        base += total;
    }
    return false;
}

bool Wm97xxDigitiser::LastResult(uint64_t frame, Result& r) const {
    for (auto it = bursts_.rbegin(); it != bursts_.rend(); ++it) {
        const uint64_t c = Conversions(*it, frame + 1u);
        if (c == 0u) continue;
        r = ResultOf(*it, c - 1u);
        return true;
    }
    if (!has_pruned_) return false;
    r = pruned_last_;
    return true;
}

void Wm97xxDigitiser::Prune(uint32_t slot, uint64_t frames) {
    frontier_[slot] = std::max(frontier_[slot], frames);
    prune_mask_ |= 1u << slot;
    uint64_t settled = frames;
    for (uint32_t s = 0u; s < kSlots; ++s) {
        if ((prune_mask_ & (1u << s)) != 0u) settled = std::min(settled, frontier_[s]);
    }
    while (bursts_.size() > 1u && LastFrame(bursts_.front()) < settled) {
        const Burst&   b     = bursts_.front();
        const uint64_t total = Total(b);
        if (total != 0u) {
            pruned_last_ = ResultOf(b, total - 1u);
            has_pruned_  = true;
        }
        pruned_slots_[b.seq.slot] += total;
        pruned_results_ += total;
        bursts_.erase(bursts_.begin());
    }
    const uint64_t keep = bursts_.empty() ? settled : std::min(settled, bursts_.front().start);
    size_t drop = 0;
    while (drop < edges_.size() && edges_[drop].frame <= keep) {
        pen0_ = edges_[drop].down;
        ++drop;
    }
    edges_.erase(edges_.begin(), edges_.begin() + static_cast<std::ptrdiff_t>(drop));
}

namespace {

void SaveSequence(StateWriter& w, const Wm97xxDigitiser::Sequence& s) {
    w.Write<uint32_t>("dig_seq_period", s.period);
    w.Write<uint32_t>("dig_seq_count", s.count);
    w.Write<uint32_t>("dig_seq_group", s.group);
    for (uint8_t t : s.tags) w.Write<uint8_t>("dig_seq_tag", t);
    for (uint32_t f : s.steps) w.Write<uint32_t>("dig_seq_step", f);
    w.Write<uint8_t>("dig_seq_pen_gated", s.pen_gated ? 1u : 0u);
    w.Write<uint8_t>("dig_seq_slot", s.slot);
    w.Write<uint8_t>("dig_seq_hold", s.hold ? 1u : 0u);
}

void RestoreSequence(StateReader& r, Wm97xxDigitiser::Sequence& s) {
    uint8_t gated = 0, hold = 0;
    r.Read("dig_seq_period", s.period);
    r.Read("dig_seq_count", s.count);
    r.Read("dig_seq_group", s.group);
    for (uint8_t& t : s.tags) r.Read("dig_seq_tag", t);
    for (uint32_t& f : s.steps) r.Read("dig_seq_step", f);
    r.Read("dig_seq_pen_gated", gated);
    r.Read("dig_seq_slot", s.slot);
    r.Read("dig_seq_hold", hold);
    s.pen_gated = gated != 0u;
    s.hold      = hold != 0u;
}

}  // namespace

void Wm97xxDigitiser::Save(StateWriter& w) const {
    w.Write<uint32_t>("dig_bursts", static_cast<uint32_t>(bursts_.size()));
    for (const Burst& b : bursts_) {
        SaveSequence(w, b.seq);
        w.Write<uint64_t>("dig_burst_start", b.start);
        w.Write<uint64_t>("dig_burst_sets", b.sets);
        w.Write<uint64_t>("dig_burst_cut", b.cut);
        w.Write<uint8_t>("dig_burst_polled", b.polled ? 1u : 0u);
        w.Write<uint64_t>("dig_burst_first", b.first);
        w.Write<uint64_t>("dig_burst_cap", b.cap);
    }
    w.Write<uint32_t>("dig_edges", static_cast<uint32_t>(edges_.size()));
    for (const Edge& e : edges_) {
        w.Write<uint64_t>("dig_edge_frame", e.frame);
        w.Write<uint8_t>("dig_edge_down", e.down ? 1u : 0u);
    }
    w.Write<uint8_t>("dig_pen0", pen0_ ? 1u : 0u);
    w.Write<uint8_t>("dig_continuous", continuous_ ? 1u : 0u);
    SaveSequence(w, config_);
    for (uint64_t n : pruned_slots_) w.Write<uint64_t>("dig_pruned_slot", n);
    for (uint64_t f : frontier_) w.Write<uint64_t>("dig_frontier", f);
    w.Write<uint32_t>("dig_prune_mask", prune_mask_);
    w.Write<uint64_t>("dig_pruned_results", pruned_results_);
    w.Write<uint64_t>("dig_read", read_);
    w.Write<uint8_t>("dig_has_pruned", has_pruned_ ? 1u : 0u);
    w.Write<uint8_t>("dig_pruned_tag", pruned_last_.tag);
    w.Write<uint8_t>("dig_pruned_pen", pruned_last_.pen_down ? 1u : 0u);
}

void Wm97xxDigitiser::Restore(StateReader& r) {
    uint32_t bursts = 0, edges = 0;
    r.Read("dig_bursts", bursts);
    bursts_.assign(bursts, Burst{});
    for (Burst& b : bursts_) {
        uint8_t polled = 0;
        RestoreSequence(r, b.seq);
        r.Read("dig_burst_start", b.start);
        r.Read("dig_burst_sets", b.sets);
        r.Read("dig_burst_cut", b.cut);
        r.Read("dig_burst_polled", polled);
        r.Read("dig_burst_first", b.first);
        r.Read("dig_burst_cap", b.cap);
        b.polled = polled != 0u;
    }
    r.Read("dig_edges", edges);
    edges_.assign(edges, Edge{});
    for (Edge& e : edges_) {
        uint8_t down = 0;
        r.Read("dig_edge_frame", e.frame);
        r.Read("dig_edge_down", down);
        e.down = down != 0u;
    }
    uint8_t pen0 = 0, continuous = 0, has_pruned = 0, pruned_pen = 0;
    r.Read("dig_pen0", pen0);
    r.Read("dig_continuous", continuous);
    RestoreSequence(r, config_);
    for (uint64_t& n : pruned_slots_) r.Read("dig_pruned_slot", n);
    for (uint64_t& f : frontier_) r.Read("dig_frontier", f);
    r.Read("dig_prune_mask", prune_mask_);
    r.Read("dig_pruned_results", pruned_results_);
    r.Read("dig_read", read_);
    r.Read("dig_has_pruned", has_pruned);
    r.Read("dig_pruned_tag", pruned_last_.tag);
    r.Read("dig_pruned_pen", pruned_pen);
    pen0_                 = pen0 != 0u;
    continuous_           = continuous != 0u;
    has_pruned_           = has_pruned != 0u;
    pruned_last_.pen_down = pruned_pen != 0u;
}
