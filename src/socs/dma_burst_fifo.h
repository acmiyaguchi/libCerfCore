#pragma once

#include <cstdint>

class DmaBurstFifo {
public:
    static constexpr uint64_t kUnlimited = UINT64_MAX;

    struct Arrivals {
        uint64_t refill_words  = 0;
        uint64_t refill_landed = 0;
        uint64_t bursts        = 0;
        uint64_t last_words    = 0;
    };

    void Configure(uint32_t depth, uint32_t threshold, uint32_t burst);
    void Clear() { level_ = 0u; }
    void ResetMoved() { moved_ = 0u; }
    void SetSupply(uint64_t words) { supply_ = words; }

    uint32_t Threshold() const { return threshold_; }
    uint32_t Burst() const     { return burst_; }
    uint32_t Level() const     { return level_; }
    uint64_t Moved() const     { return moved_; }
    uint64_t Supply() const    { return supply_; }

    void     Put();
    void     Refill(Arrivals* arrivals = nullptr);
    uint64_t Take(uint64_t n, Arrivals* arrivals = nullptr);
    bool     TakesToMove(uint64_t words, uint64_t& n) const;
    uint64_t TakesBeforeEmpty() const;
    uint64_t Landing() const;

    void Restore(uint32_t level, uint64_t moved) {
        level_ = level;
        moved_ = moved;
    }

private:
    uint64_t BurstsAvailable() const;
    uint64_t WordsIn(uint64_t bursts) const;
    uint64_t LandedIn(uint64_t bursts) const;

    uint32_t depth_     = 0;
    uint32_t threshold_ = 0;
    uint32_t burst_     = 0;
    uint32_t level_     = 0;
    uint64_t moved_     = 0;
    uint64_t supply_    = 0;
};
