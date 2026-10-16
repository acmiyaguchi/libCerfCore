#pragma once

#include <cerfcore/core/service.h>
#include "../../execution/guest_cycle_clock.h"
#include "../rated_tick_count.h"

#include <cstdint>

namespace cerfcore {

class Sa11xxExternalClocks;
class StateReader;
class StateWriter;

class Sa11xxMcpFrameClock : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    void SetControl(uint32_t mccr0, uint32_t mccr1);
    void Freeze(uint64_t now);
    bool Resume(uint64_t now);
    void Retime(uint64_t now);
    void OnCpuRate(uint64_t now);

    static uint64_t SclkHzFor(uint32_t mccr1);

    bool     Mce() const;
    bool     Enabled() const { return enabled_; }
    bool     Held() const { return held_; }
    bool     SclkTick(uint64_t now, uint64_t& tick) const;
    uint64_t SclkNow(uint64_t now) const { return enabled_ ? sclk_.TicksAt(now) : frozen_tick_; }
    uint64_t NextFrameEdge(uint64_t now) const;
    uint64_t CycleOfSclk(uint64_t tick) const { return sclk_.CycleOfTick(tick); }
    GuestCycleClock::Rate WordClock() const;
    bool     SclkRateFor(uint32_t mccr0, uint32_t mccr1, GuestCycleClock::Rate& rate) const;
    uint64_t RunBase() const { return run_base_; }
    uint64_t FrozenTick() const { return frozen_tick_; }

    void Save(StateWriter& w, uint64_t now) const;
    void Restore(StateReader& r);

private:
    void     Disable();
    uint64_t SclkHz() const { return SclkHzFor(mccr1_); }

    GuestCycleClock*         clock_ = nullptr;
    Sa11xxExternalClocks*    ext_   = nullptr;
    NarrowRatedTickCount     sclk_;
    TickPosition             held_pos_{};
    GuestCycleClock::Rate    rate_{};
    bool     enabled_     = false;
    bool     held_        = false;
    uint64_t frozen_tick_ = 0;
    uint64_t run_base_    = 0;
    uint32_t mccr0_       = 0;
    uint32_t mccr1_       = 0;
};

}
