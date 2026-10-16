#pragma once

#include "../execution/guest_cycle_clock.h"
#include "rated_tick_count.h"

#include <cstdint>

struct RasterFrame {
    static constexpr uint32_t kMaxEdges = 2u;

    uint64_t ticks = 1;
    uint64_t edge[kMaxEdges] = {};
    uint32_t edges = 0;
};

template <class Units>
class BasicRasterScanClock {
public:
    using Frame    = RasterFrame;
    using Position = TickPosition;

    bool Start(uint64_t cycle, GuestCycleClock::Rate core, GuestCycleClock::Rate tick,
               const Frame& frame);
    bool Resume(uint64_t cycle, GuestCycleClock::Rate core, GuestCycleClock::Rate tick,
                const Frame& frame, const Position& at);
    bool Rescale(uint64_t cycle, GuestCycleClock::Rate core, GuestCycleClock::Rate tick);

    Position PositionAt(uint64_t cycle) const { return ticks_.PositionAt(cycle); }
    uint64_t CycleOfTick(uint64_t tick) const { return ticks_.CycleOfTick(tick); }
    uint64_t TickInFrame(uint64_t cycle) const { return ticks_.TicksAt(cycle) % frame_.ticks; }
    bool     EdgeCycle(uint64_t edge, uint64_t& cycle) const;

    uint64_t EdgesThrough(uint64_t cycle) const {
        const uint64_t ticks    = ticks_.TicksAt(cycle);
        const uint64_t in_frame = ticks % frame_.ticks;
        uint64_t       edges    = ticks / frame_.ticks * frame_.edges;
        for (uint32_t i = 0; i < frame_.edges; ++i) {
            if (frame_.edge[i] <= in_frame) ++edges;
        }
        return edges;
    }

    uint32_t EdgesPerFrame() const { return frame_.edges; }

private:
    static bool FrameValid(const Frame& frame);
    bool        SetFrame(const Frame& frame);

    BasicRatedTickCount<Units> ticks_;
    Frame                      frame_;
};

using RasterScanClock       = BasicRasterScanClock<ExactCounterUnits>;
using NarrowRasterScanClock = BasicRasterScanClock<NarrowCounterUnits>;
