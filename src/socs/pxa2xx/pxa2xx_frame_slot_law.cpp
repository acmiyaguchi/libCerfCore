#include "pxa2xx_frame_slot_law.h"

#include <cerfcore/state/state_stream.h>

#include <algorithm>
#include <limits>
#include <string>

namespace cerfcore {

namespace {

constexpr uint64_t kOpen = std::numeric_limits<uint64_t>::max();

}  // namespace

void Pxa2xxFrameSlotLaw::Reset(uint64_t frame, Pxa2xxSlotPace pace) {
    base_count_ = 0u;
    segments_.assign(1u, Segment{frame, pace});
}

void Pxa2xxFrameSlotLaw::Change(uint64_t frame, Pxa2xxSlotPace pace) {
    if (segments_.empty()) {
        Reset(frame, pace);
        return;
    }
    if (segments_.back().pace == pace) return;
    if (segments_.back().start >= frame) {
        segments_.back().pace = pace;
        return;
    }
    segments_.push_back(Segment{frame, pace});
}

void Pxa2xxFrameSlotLaw::Prune(uint64_t settled) {
    while (segments_.size() > 1u && segments_[1].start <= settled) {
        base_count_ += Slots(segments_[1].start - segments_[0].start, segments_[0].pace);
        segments_.erase(segments_.begin());
    }
}

Pxa2xxSlotPace Pxa2xxFrameSlotLaw::PaceAt(uint64_t frame) const {
    Pxa2xxSlotPace pace;
    for (const Segment& s : segments_) {
        if (s.start > frame) break;
        pace = s.pace;
    }
    return pace;
}

/* AC '97 Component Specification Revision 2.1 Appendix A.3 (page 62): "in passing a 44.1 kHz
   stream across the AC-link, for every 480 audio output frames that are sent across, 441 of
   them must contain valid sample data". */
uint64_t Pxa2xxFrameSlotLaw::Count(uint64_t frames) const {
    uint64_t acc = base_count_;
    for (size_t i = 0; i < segments_.size(); ++i) {
        const uint64_t start = segments_[i].start;
        if (frames <= start) break;
        const uint64_t end = i + 1u < segments_.size() ? segments_[i + 1u].start : kOpen;
        acc += Slots(std::min(frames, end) - start, segments_[i].pace);
    }
    return acc;
}

bool Pxa2xxFrameSlotLaw::FrameOfSlot(uint64_t slot, uint64_t& frame) const {
    uint64_t acc = base_count_;
    for (size_t i = 0; i < segments_.size(); ++i) {
        const Segment& s = segments_[i];
        const bool last = i + 1u == segments_.size();
        const uint64_t in_segment = last ? 0u : Slots(segments_[i + 1u].start - s.start, s.pace);
        if (s.pace.rate != 0u && (last || slot <= acc + in_segment)) {
            const uint64_t j   = slot - acc;
            const uint64_t per = static_cast<uint64_t>(frame_rate_) * s.pace.div;
            frame = s.start + (j * per + s.pace.rate - 1u) / s.pace.rate - 1u;
            return true;
        }
        if (last) return false;
        acc += in_segment;
    }
    return false;
}

void Pxa2xxFrameSlotLaw::Save(StateWriter& w, const char* prefix) const {
    const std::string p = prefix;
    w.Write<uint64_t>((p + "_law_base").c_str(), base_count_);
    w.Write<uint32_t>((p + "_law_segments").c_str(), static_cast<uint32_t>(segments_.size()));
    for (const Segment& s : segments_) {
        w.Write<uint64_t>((p + "_law_start").c_str(), s.start);
        w.Write<uint32_t>((p + "_law_rate").c_str(), s.pace.rate);
        w.Write<uint32_t>((p + "_law_div").c_str(), s.pace.div);
    }
}

void Pxa2xxFrameSlotLaw::Restore(StateReader& r, const char* prefix) {
    const std::string p = prefix;
    uint32_t n = 0;
    r.Read((p + "_law_base").c_str(), base_count_);
    r.Read((p + "_law_segments").c_str(), n);
    segments_.clear();
    for (uint32_t i = 0; i < n; ++i) {
        Segment s{};
        r.Read((p + "_law_start").c_str(), s.start);
        r.Read((p + "_law_rate").c_str(), s.pace.rate);
        r.Read((p + "_law_div").c_str(), s.pace.div);
        segments_.push_back(s);
    }
}

}
