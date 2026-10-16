#pragma once

#include "../core/tick_scale.h"
#include "../execution/guest_cycle_clock.h"

#include <cstdint>

class StateReader;
class StateWriter;

struct CounterPhase {
    WideUint<3> num;
    WideUint<3> den = WideUint<3>::Of(1u);

    static CounterPhase Of(uint64_t num, uint64_t den) {
        CounterPhase p;
        p.num = WideUint<3>::Of(num);
        p.den = WideUint<3>::Of(den);
        return p;
    }

    static CounterPhase LastInputClock(uint64_t divide) { return Of(divide - 1u, divide); }

    void Save(StateWriter& w, const char* prefix) const;
    void Restore(StateReader& r, const char* prefix);
};

class ExactCounterUnits {
public:
    static bool FitsRatio(const WideUint<2>&, const WideUint<2>&) { return true; }

    void Set(const WideUint<2>& cycles, const WideUint<2>& ticks) {
        ticks_per_cycle_.Set(ticks, cycles);
        cycles_per_tick_.Set(cycles, ticks);
    }

    uint64_t Ticks(uint64_t cycles) const { return ticks_per_cycle_.Floor(cycles); }
    uint64_t TickRemainder(uint64_t cycles) const { return ticks_per_cycle_.RemainderU64(cycles); }

    uint64_t TicksRem(uint64_t cycles, uint64_t& rem) const {
        return ticks_per_cycle_.FloorRemU64(cycles, rem);
    }

    uint64_t CycleDenU64() const { return ticks_per_cycle_.DenU64(); }
    uint64_t CyclesCeil(uint64_t ticks) const { return cycles_per_tick_.Ceil(ticks); }

    WideUint<2> WideTickRemainder(uint64_t cycles) const {
        return ticks_per_cycle_.Remainder(cycles);
    }

    const WideUint<2>& CycleUnit() const { return ticks_per_cycle_.Den(); }
    const WideUint<2>& TickUnit() const { return ticks_per_cycle_.Num(); }

private:
    ExactScale ticks_per_cycle_;
    ExactScale cycles_per_tick_;
};

class NarrowCounterUnits {
public:
    static bool FitsRatio(const WideUint<2>& cycles, const WideUint<2>& ticks) {
        return NarrowFloorFits(ticks, cycles) && NarrowCeilFits(cycles, ticks);
    }

    void Set(const WideUint<2>& cycles, const WideUint<2>& ticks) {
        cyc_ = cycles.w[0];
        tk_  = ticks.w[0];
    }

    uint64_t Ticks(uint64_t cycles) const { return ScaleU64(cycles, tk_, cyc_); }
    uint64_t TickRemainder(uint64_t cycles) const { return ScaleRemainderU64(cycles, tk_, cyc_); }

    uint64_t TicksRem(uint64_t cycles, uint64_t& rem) const {
        return ScaleFloorRemU64(cycles, tk_, cyc_, rem);
    }

    uint64_t CycleDenU64() const { return cyc_; }
    uint64_t CyclesCeil(uint64_t ticks) const { return ScaleU64Ceil(ticks, cyc_, tk_); }

    WideUint<2> WideTickRemainder(uint64_t cycles) const {
        return WideUint<2>::Of(TickRemainder(cycles));
    }

    WideUint<2> CycleUnit() const { return WideUint<2>::Of(cyc_); }
    WideUint<2> TickUnit() const { return WideUint<2>::Of(tk_); }

private:
    uint64_t cyc_ = 1;
    uint64_t tk_  = 1;
};

template <class Units>
class BasicCycleAnchoredCounter {
public:
    using Phase = CounterPhase;

    bool SetRatio(uint64_t cycles, uint64_t ticks);
    bool Rescale(uint64_t cycle, uint64_t cycles, uint64_t ticks);
    bool SetRate(GuestCycleClock::Rate core, GuestCycleClock::Rate tick);
    bool RescaleRate(uint64_t cycle, GuestCycleClock::Rate core, GuestCycleClock::Rate tick);
    bool AnchorAtPhase(uint64_t cycle, uint32_t count, uint64_t phase, uint64_t denominator);
    void AnchorAt(uint64_t cycle, uint32_t count, const Phase& phase);
    Phase TickPhaseAt(uint64_t cycle) const;
    /* MCIMX51RM Figure 29-8 (page 29-11), MCIMX31RM Figure 33-9 (page 33-14): after a
       prescaler value change the next prescaled pulse comes on the next input clock edge. */
    Phase DivideChangePhase(uint64_t cycle, uint64_t old_divide, uint64_t new_divide) const;

    void Anchor(uint64_t cycle, uint32_t count) {
        anchor_cycle_ = cycle;
        anchor_count_ = count;
    }

    void SetCountAt(uint64_t cycle, uint32_t count) {
        anchor_count_ = count - static_cast<uint32_t>(TicksSince(cycle));
    }

    uint64_t PhaseAt(uint64_t cycle) const { return units_.TickRemainder(cycle - anchor_cycle_); }
    uint64_t PhaseDenominator() const { return units_.CycleDenU64(); }

    uint64_t TicksSincePhase(uint64_t cycle, uint64_t& phase) const {
        return units_.TicksRem(cycle - anchor_cycle_, phase);
    }

    uint64_t AnchorCycle() const { return anchor_cycle_; }
    uint32_t AnchorCount() const { return anchor_count_; }

    uint64_t TicksSince(uint64_t cycle) const { return units_.Ticks(cycle - anchor_cycle_); }

    uint32_t CountAt(uint64_t cycle) const {
        return anchor_count_ + static_cast<uint32_t>(TicksSince(cycle));
    }

    uint64_t CycleOfTick(uint64_t ticks_after_anchor) const {
        return anchor_cycle_ + units_.CyclesCeil(ticks_after_anchor);
    }

    uint64_t NextMatchCycle(uint32_t match, uint64_t cycle) const {
        const uint64_t since = TicksSince(cycle);
        const uint32_t d     = match - (anchor_count_ + static_cast<uint32_t>(since));
        return CycleOfTick(since + (d != 0u ? d : kCounterWrapTicks));
    }

private:
    static constexpr uint64_t kCounterWrapTicks = 0x100000000ull;

    static bool RateRatio(GuestCycleClock::Rate core, GuestCycleClock::Rate tick,
                          WideUint<2>& cycles, WideUint<2>& ticks);
    static bool ReduceRatio(uint64_t cycles, uint64_t ticks, WideUint<2>& cyc, WideUint<2>& tk);

    void ReplaceUnits(uint64_t cycle, const WideUint<2>& cycles, const WideUint<2>& ticks);
    void PlaceAnchor(uint64_t cycle, uint32_t count, const Phase& phase);

    uint32_t anchor_count_ = 0;
    uint64_t anchor_cycle_ = 0;
    Units    units_;
};

using CycleAnchoredCounter       = BasicCycleAnchoredCounter<ExactCounterUnits>;
using NarrowCycleAnchoredCounter = BasicCycleAnchoredCounter<NarrowCounterUnits>;
