#pragma once

#include <cstdint>
#include <vector>

class StateReader;
class StateWriter;

class Sa11xxMcpReceiveSchedule {
public:
    static constexpr uint64_t kNever = UINT64_MAX;

    static uint64_t NextFrameReturn(uint64_t tick);

    void Open(uint64_t origin, uint64_t period, uint64_t first_valid);
    void OpenEveryFrame(uint64_t first_frame);
    void Limit(uint64_t limit);
    void Reload(uint64_t origin, uint64_t period);
    void Append(uint64_t limit, uint64_t origin, uint64_t period, uint64_t k_lo);
    void Rephase(uint64_t at, uint64_t period);
    void Advance(uint64_t pushed);
    void Close() { open_ = false; segments_.clear(); }

    bool     IsOpen() const { return open_; }
    bool     Pending(uint64_t pushed) const;
    uint64_t PushesBy(uint64_t tick) const;
    bool     TickOfPush(uint64_t index, uint64_t& tick) const;

    void Save(StateWriter& w) const;
    void Restore(StateReader& r);

private:
    struct Segment {
        uint64_t origin = 0;
        uint64_t period = 1;
        uint64_t k_lo   = 1;
        uint64_t k_hi   = kNever;
    };

    static uint64_t Count(const Segment& s);
    static uint64_t PushTick(const Segment& s, uint64_t k);
    static uint64_t SegmentPushesBy(const Segment& s, uint64_t tick);

    bool                 open_   = false;
    uint64_t             base_   = 0;
    std::vector<Segment> segments_;
};
