#pragma once

#include <cstdint>
#include <vector>

namespace cerfcore {

class StateReader;
class StateWriter;

class Wm97xxDigitiser {
public:
    static constexpr uint32_t kMaxTags = 21u;
    static constexpr uint32_t kSlots   = 13u;

    struct Sequence {
        uint32_t period    = 0;
        uint32_t count     = 0;
        uint32_t group     = 0;
        uint8_t  tags[kMaxTags]  = {};
        uint32_t steps[kMaxTags] = {};
        bool     pen_gated = false;
        uint8_t  slot      = 0;
        bool     hold      = false;

        void Add(uint8_t tag, uint32_t frames);
        bool operator==(const Sequence& o) const;
    };

    struct Result {
        uint8_t tag      = 0;
        bool    pen_down = false;
    };

    void Clear(bool pen_down);
    void ClearHistory(bool pen_down);
    void SetContinuous(uint64_t frame, const Sequence* seq);
    void StartPolled(uint64_t frame, const Sequence& seq);
    void SetPen(uint64_t frame, bool down);
    void ReadResult(uint64_t frame);

    bool PenDownAt(uint64_t frame) const;
    bool Polling(uint64_t frame) const;

    uint64_t SlotWordsBefore(uint32_t slot, uint64_t frames) const;
    bool     FrameOfSlotWord(uint32_t slot, uint64_t n, uint64_t& frame) const;
    bool     SlotWord(uint32_t slot, uint64_t n, Result& r) const;
    bool     LastResult(uint64_t frame, Result& r) const;
    void     Prune(uint32_t slot, uint64_t frames);

    void Save(StateWriter& w) const;
    void Restore(StateReader& r);

private:
    static constexpr uint64_t kOpen = UINT64_MAX;

    struct Burst {
        Sequence seq;
        uint64_t start  = 0;
        uint64_t sets   = 0;
        uint64_t cut    = 0;
        bool     polled = false;
        uint64_t first  = 0;
        uint64_t cap    = kOpen;
    };

    struct Edge {
        uint64_t frame = 0;
        bool     down  = false;
    };

    static uint32_t SetsPerCycle(const Burst& b) { return b.seq.count / b.seq.group; }
    static uint64_t SetSpan(const Burst& b, uint32_t set);
    static uint64_t SetPeriod(const Burst& b, uint32_t set);
    static uint64_t SetOffset(const Burst& b, uint32_t set);
    static uint64_t CycleFrames(const Burst& b);
    static uint64_t ConversionOffset(const Burst& b, uint32_t i);
    static uint64_t DoneInCycle(const Burst& b, uint64_t frames);
    static uint64_t Pos(const Burst& b, uint64_t i);
    static uint64_t ResultFrame(const Burst& b, uint64_t k);
    static uint64_t Conversions(const Burst& b, uint64_t frames, bool capped = true);
    static uint64_t SetsCheckedBefore(const Burst& b, uint64_t frame);
    static uint64_t Total(const Burst& b, bool capped = true);
    static uint64_t LastFrame(const Burst& b);

    void     CloseLast(uint64_t frame);
    void     OpenBurst(uint64_t start, const Sequence& seq, bool polled);
    uint64_t ResultsBefore(uint64_t frames) const;
    Result   ResultOf(const Burst& b, uint64_t k) const;

    std::vector<Burst> bursts_;
    std::vector<Edge>  edges_;
    bool     pen0_         = false;
    bool     continuous_   = false;
    Sequence config_;
    uint64_t pruned_slots_[kSlots] = {};
    uint64_t frontier_[kSlots]     = {};
    uint32_t prune_mask_      = 0;
    uint64_t pruned_results_  = 0;
    uint64_t read_            = 0;
    bool     has_pruned_   = false;
    Result   pruned_last_;
};

}
