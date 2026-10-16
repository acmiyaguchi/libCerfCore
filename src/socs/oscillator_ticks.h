#pragma once

#include "../execution/guest_cycle_clock.h"
#include "rated_tick_count.h"

#include <cstdint>

class CerfEmulator;
class GuestDeepSleep;
class StateReader;
class StateWriter;

class OscillatorTicks {
public:
    OscillatorTicks(CerfEmulator& emu, bool credits_park) : emu_(emu), credits_park_(credits_park) {}
    virtual ~OscillatorTicks() = default;

    struct Reading {
        uint64_t total;
        uint64_t park;
    };

    void Attach(uint64_t osc_num, uint64_t osc_den);

    uint64_t Now() {
        DrainPark();
        return ticks_.TicksAt(ClockCycles());
    }

    Reading Sample() { return Reading{Now(), park_ticks_}; }

    uint64_t CycleOf(uint64_t tick);
    void     ArmAt(GuestCycleClock::Event* event, uint64_t tick);
    int64_t  SleptNsAtTick(uint64_t tick);
    void     SetOscRate(uint64_t osc_num, uint64_t osc_den);
    void     Rescale();
    void     Rebase();

    GuestCycleClock::Rate OscRate() const { return GuestCycleClock::Rate{osc_num_, osc_den_}; }

    virtual void Save(StateWriter& w, const char* prefix = "");
    virtual void Restore(StateReader& r, const char* prefix = "");

protected:
    virtual uint64_t ClockCycles();
    virtual bool     ClockStopped() { return false; }

    void DrainPark();

    CerfEmulator&    emu_;
    GuestCycleClock* clock_ = nullptr;
    GuestDeepSleep*  sleep_ = nullptr;

private:
    uint64_t Scale() const;
    void     RequireOscRate(uint64_t num, uint64_t den) const;
    void     SetRatio();
    void     RescaleAt(uint64_t now);
    uint64_t CreditNs(uint64_t ns);

    const bool           credits_park_;
    RatedTickCount       ticks_;
    uint64_t             osc_num_    = 1;
    uint64_t             osc_den_    = 1;
    uint64_t             park_ticks_ = 0;
    uint64_t             credit_rem_ = 0;
    int64_t              slept_seen_ = 0;
};

class StoppableOscillatorTicks : public OscillatorTicks {
public:
    using OscillatorTicks::OscillatorTicks;

    void SetCounting(bool counting);

    void Save(StateWriter& w, const char* prefix = "") override;
    void Restore(StateReader& r, const char* prefix = "") override;

protected:
    uint64_t ClockCycles() override;
    bool     ClockStopped() override { return stopped_; }

private:
    bool     stopped_      = false;
    uint64_t stopped_at_   = 0;
    uint64_t stopped_span_ = 0;
};
