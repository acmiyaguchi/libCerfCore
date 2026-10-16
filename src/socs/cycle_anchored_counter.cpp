#include "cycle_anchored_counter.h"

#include <cerfcore/state/state_stream.h>

#include <numeric>
#include <string>

namespace cerfcore {

void CounterPhase::Save(StateWriter& w, const char* prefix) const {
    const std::string p(prefix);
    w.WriteBytes((p + "_num").c_str(), num.w, sizeof(num.w));
    w.WriteBytes((p + "_den").c_str(), den.w, sizeof(den.w));
}

void CounterPhase::Restore(StateReader& r, const char* prefix) {
    const std::string p(prefix);
    r.ReadBytes((p + "_num").c_str(), num.w, sizeof(num.w));
    r.ReadBytes((p + "_den").c_str(), den.w, sizeof(den.w));
}

template <class Units>
bool BasicCycleAnchoredCounter<Units>::RateRatio(GuestCycleClock::Rate core,
                                                 GuestCycleClock::Rate tick, WideUint<2>& cycles,
                                                 WideUint<2>& ticks) {
    if (core.num == 0u || core.den == 0u || tick.num == 0u || tick.den == 0u) return false;
    core = core.Reduced();
    tick = tick.Reduced();
    const uint64_t gn = std::gcd(core.num, tick.num);
    const uint64_t gd = std::gcd(core.den, tick.den);
    cycles = MultiplyWide(WideUint<1>::Of(core.num / gn), WideUint<1>::Of(tick.den / gd));
    ticks  = MultiplyWide(WideUint<1>::Of(core.den / gd), WideUint<1>::Of(tick.num / gn));
    return Units::FitsRatio(cycles, ticks);
}

template <class Units>
bool BasicCycleAnchoredCounter<Units>::ReduceRatio(uint64_t cycles, uint64_t ticks,
                                                   WideUint<2>& cyc, WideUint<2>& tk) {
    if (cycles == 0u || ticks == 0u) return false;
    const uint64_t g = std::gcd(cycles, ticks);
    cyc = WideUint<2>::Of(cycles / g);
    tk  = WideUint<2>::Of(ticks / g);
    return Units::FitsRatio(cyc, tk);
}

template <class Units>
bool BasicCycleAnchoredCounter<Units>::SetRatio(uint64_t cycles, uint64_t ticks) {
    WideUint<2> cyc, tk;
    if (!ReduceRatio(cycles, ticks, cyc, tk)) return false;
    units_.Set(cyc, tk);
    return true;
}

template <class Units>
bool BasicCycleAnchoredCounter<Units>::Rescale(uint64_t cycle, uint64_t cycles, uint64_t ticks) {
    WideUint<2> cyc, tk;
    if (!ReduceRatio(cycles, ticks, cyc, tk)) return false;
    ReplaceUnits(cycle, cyc, tk);
    return true;
}

template <class Units>
bool BasicCycleAnchoredCounter<Units>::SetRate(GuestCycleClock::Rate core,
                                               GuestCycleClock::Rate tick) {
    WideUint<2> cycles, ticks;
    if (!RateRatio(core, tick, cycles, ticks)) return false;
    units_.Set(cycles, ticks);
    return true;
}

template <class Units>
bool BasicCycleAnchoredCounter<Units>::RescaleRate(uint64_t cycle, GuestCycleClock::Rate core,
                                                   GuestCycleClock::Rate tick) {
    WideUint<2> cycles, ticks;
    if (!RateRatio(core, tick, cycles, ticks)) return false;
    ReplaceUnits(cycle, cycles, ticks);
    return true;
}

template <class Units>
bool BasicCycleAnchoredCounter<Units>::AnchorAtPhase(uint64_t cycle, uint32_t count,
                                                     uint64_t phase, uint64_t denominator) {
    if (denominator == 0u || phase >= denominator) return false;
    PlaceAnchor(cycle, count, Phase::Of(phase, denominator));
    return true;
}

template <class Units>
void BasicCycleAnchoredCounter<Units>::AnchorAt(uint64_t cycle, uint32_t count,
                                                const Phase& phase) {
    PlaceAnchor(cycle, count, phase);
}

template <class Units>
CounterPhase BasicCycleAnchoredCounter<Units>::TickPhaseAt(uint64_t cycle) const {
    Phase p;
    p.num = units_.WideTickRemainder(cycle - anchor_cycle_).template Resized<3>();
    p.den = units_.CycleUnit().template Resized<3>();
    return p;
}

template <class Units>
CounterPhase BasicCycleAnchoredCounter<Units>::DivideChangePhase(uint64_t cycle,
                                                                 uint64_t old_divide,
                                                                 uint64_t new_divide) const {
    const WideUint<2>& cycles = units_.CycleUnit();
    const WideUint<2>  part   = units_.WideTickRemainder(cycle - anchor_cycle_);
    WideUint<2>        edge;
    DivideWide(MultiplyWide(part, WideUint<1>::Of(old_divide)), cycles, &edge);
    Phase p;
    p.num = AddWide(MultiplyWide(cycles, WideUint<1>::Of(new_divide - 1u)), edge);
    p.den = MultiplyWide(cycles, WideUint<1>::Of(new_divide));
    return p;
}

template <class Units>
void BasicCycleAnchoredCounter<Units>::ReplaceUnits(uint64_t cycle, const WideUint<2>& cycles,
                                                    const WideUint<2>& ticks) {
    const Phase    phase = TickPhaseAt(cycle);
    const uint32_t count = CountAt(cycle);
    units_.Set(cycles, ticks);
    PlaceAnchor(cycle, count, phase);
}

template <class Units>
void BasicCycleAnchoredCounter<Units>::PlaceAnchor(uint64_t cycle, uint32_t count,
                                                   const Phase& phase) {
    anchor_count_ = count;
    anchor_cycle_ = cycle - DivideWide(MultiplyWide(phase.num, units_.CycleUnit()),
                                       MultiplyWide(phase.den, units_.TickUnit()), nullptr);
}

template class BasicCycleAnchoredCounter<ExactCounterUnits>;
template class BasicCycleAnchoredCounter<NarrowCounterUnits>;

}
