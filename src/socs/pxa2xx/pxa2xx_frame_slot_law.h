#pragma once

#include "pxa2xx_slot_source.h"

#include <cstdint>
#include <vector>

namespace cerfcore {

class StateReader;
class StateWriter;

struct Pxa2xxSlotPace {
    uint32_t rate = 0;
    uint32_t div  = 1;

    bool operator==(const Pxa2xxSlotPace& o) const { return rate == o.rate && div == o.div; }
    bool operator!=(const Pxa2xxSlotPace& o) const { return !(*this == o); }
};

class Pxa2xxFrameSlotLaw : public Pxa2xxSlotSource {
public:
    explicit Pxa2xxFrameSlotLaw(uint32_t frame_rate) : frame_rate_(frame_rate) {}

    void Reset(uint64_t frame, Pxa2xxSlotPace pace);
    void Change(uint64_t frame, Pxa2xxSlotPace pace);
    void Prune(uint64_t settled) override;

    Pxa2xxSlotPace PaceAt(uint64_t frame) const;
    uint64_t       Count(uint64_t frames) const override;
    bool           FrameOfSlot(uint64_t slot, uint64_t& frame) const override;

    void Save(StateWriter& w, const char* prefix) const;
    void Restore(StateReader& r, const char* prefix);

private:
    struct Segment {
        uint64_t       start;
        Pxa2xxSlotPace pace;
    };

    uint64_t Slots(uint64_t frames, Pxa2xxSlotPace p) const {
        return frames * p.rate / (static_cast<uint64_t>(frame_rate_) * p.div);
    }

    uint32_t frame_rate_;
    uint64_t base_count_ = 0;
    std::vector<Segment> segments_;
};

}
