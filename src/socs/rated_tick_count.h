#pragma once

#include "../execution/guest_cycle_clock.h"
#include "cycle_anchored_counter.h"

#include <cstdint>

struct TickPosition {
    uint64_t ticks     = 0;
    uint64_t phase     = 0;
    uint64_t phase_den = 1;
};

template <class Units>
class BasicRatedTickCount {
public:
    using Position = TickPosition;

    bool SetRate(GuestCycleClock::Rate core, GuestCycleClock::Rate tick);
    void Start(uint64_t cycle);
    bool PlaceAt(uint64_t cycle, const Position& at);
    void PlaceAtPhase(uint64_t cycle, uint64_t ticks, const CounterPhase& phase);
    bool Rescale(uint64_t cycle, GuestCycleClock::Rate core, GuestCycleClock::Rate tick);
    bool AddTicks(uint64_t ticks);

    Position PositionAt(uint64_t cycle) const {
        Position p;
        p.ticks     = base_ + ctr_.TicksSincePhase(cycle, p.phase);
        p.phase_den = ctr_.PhaseDenominator();
        return p;
    }

    CounterPhase TickPhaseAt(uint64_t cycle) const { return ctr_.TickPhaseAt(cycle); }

    uint64_t TicksAt(uint64_t cycle) const { return base_ + ctr_.TicksSince(cycle); }

    uint64_t CycleOfTick(uint64_t tick) const {
        return ctr_.CycleOfTick(tick > base_ ? tick - base_ : 0u);
    }

private:
    BasicCycleAnchoredCounter<Units> ctr_;
    uint64_t                         base_ = 0;
};

using RatedTickCount       = BasicRatedTickCount<ExactCounterUnits>;
using NarrowRatedTickCount = BasicRatedTickCount<NarrowCounterUnits>;
