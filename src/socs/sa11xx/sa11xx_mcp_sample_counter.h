#pragma once

#include <cstdint>

class StateReader;
class StateWriter;

class Sa11xxMcpSampleCounter {
public:
    static constexpr uint64_t kNever = UINT64_MAX;

    void Reset();
    void Enable(uint64_t origin);
    void Reload(uint64_t edge);
    void Disable(uint64_t edge) { stop_ = edge; }
    void Shift(uint64_t at, uint64_t by);
    uint64_t Rephase(uint64_t period);

    bool     Counting() const { return counting_; }
    bool     Active(uint64_t tick) const { return counting_ && tick >= origin_; }

    uint64_t Settle(uint64_t tick, uint64_t period);
    bool     TakeTick(uint64_t take, uint64_t period, uint64_t& tick) const;
    bool     NextZero(uint64_t at, uint64_t period, uint64_t& zero) const;

    void Save(StateWriter& w) const;
    void Restore(StateReader& r);

private:
    uint64_t TakesLeftInRun(uint64_t period) const;

    bool     counting_ = false;
    uint64_t origin_   = 0;
    uint64_t stop_     = kNever;
    uint64_t next_     = kNever;
    uint64_t taken_    = 0;
};
