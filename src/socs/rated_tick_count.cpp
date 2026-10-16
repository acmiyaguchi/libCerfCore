#include "rated_tick_count.h"

template <class Units>
bool BasicRatedTickCount<Units>::SetRate(GuestCycleClock::Rate core, GuestCycleClock::Rate tick) {
    return ctr_.SetRate(core, tick);
}

template <class Units>
void BasicRatedTickCount<Units>::Start(uint64_t cycle) {
    ctr_.Anchor(cycle, 0u);
    base_ = 0u;
}

template <class Units>
bool BasicRatedTickCount<Units>::PlaceAt(uint64_t cycle, const Position& at) {
    if (!ctr_.AnchorAtPhase(cycle, 0u, at.phase, at.phase_den)) return false;
    base_ = at.ticks;
    return true;
}

template <class Units>
void BasicRatedTickCount<Units>::PlaceAtPhase(uint64_t cycle, uint64_t ticks,
                                              const CounterPhase& phase) {
    ctr_.AnchorAt(cycle, 0u, phase);
    base_ = ticks;
}

template <class Units>
bool BasicRatedTickCount<Units>::Rescale(uint64_t cycle, GuestCycleClock::Rate core,
                                         GuestCycleClock::Rate tick) {
    const uint64_t ticks = TicksAt(cycle);
    if (!ctr_.RescaleRate(cycle, core, tick)) return false;
    base_ = ticks;
    return true;
}

template <class Units>
bool BasicRatedTickCount<Units>::AddTicks(uint64_t ticks) {
    if (ticks > UINT64_MAX - base_) return false;
    base_ += ticks;
    return true;
}

template class BasicRatedTickCount<ExactCounterUnits>;
template class BasicRatedTickCount<NarrowCounterUnits>;
