#include "raster_scan_clock.h"

template <class Units>
bool BasicRasterScanClock<Units>::FrameValid(const Frame& frame) {
    if (frame.ticks == 0u || frame.edges == 0u || frame.edges > Frame::kMaxEdges) return false;
    uint64_t prev = 0u;
    for (uint32_t i = 0; i < frame.edges; ++i) {
        if (frame.edge[i] <= prev || frame.edge[i] > frame.ticks) return false;
        prev = frame.edge[i];
    }
    return true;
}

template <class Units>
bool BasicRasterScanClock<Units>::SetFrame(const Frame& frame) {
    if (!FrameValid(frame)) return false;
    frame_ = frame;
    return true;
}

template <class Units>
bool BasicRasterScanClock<Units>::Start(uint64_t cycle, GuestCycleClock::Rate core,
                                        GuestCycleClock::Rate tick, const Frame& frame) {
    if (!SetFrame(frame) || !ticks_.SetRate(core, tick)) return false;
    ticks_.Start(cycle);
    return true;
}

template <class Units>
bool BasicRasterScanClock<Units>::Resume(uint64_t cycle, GuestCycleClock::Rate core,
                                         GuestCycleClock::Rate tick, const Frame& frame,
                                         const Position& at) {
    return SetFrame(frame) && ticks_.SetRate(core, tick) && ticks_.PlaceAt(cycle, at);
}

template <class Units>
bool BasicRasterScanClock<Units>::Rescale(uint64_t cycle, GuestCycleClock::Rate core,
                                          GuestCycleClock::Rate tick) {
    return ticks_.Rescale(cycle, core, tick);
}

template <class Units>
bool BasicRasterScanClock<Units>::EdgeCycle(uint64_t edge, uint64_t& cycle) const {
    const uint64_t frame = edge / frame_.edges;
    if (frame > (UINT64_MAX - frame_.ticks) / frame_.ticks) return false;
    cycle = ticks_.CycleOfTick(frame * frame_.ticks + frame_.edge[edge % frame_.edges]);
    return true;
}

template class BasicRasterScanClock<ExactCounterUnits>;
template class BasicRasterScanClock<NarrowCounterUnits>;
