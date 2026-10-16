#pragma once

#include <cerfcore/core/service.h>
#include "../../execution/guest_cycle_clock.h"
#include "../rated_tick_count.h"

#include <cstdint>
#include <functional>

namespace cerfcore {

class Pxa255ClockManager;
class Pxa255Gpio;
class Pxa255I2sBitClockInput;
class StateReader;
class StateWriter;

class Pxa255I2sBitClock : public Service {
public:
    using Service::Service;

    struct Inputs {
        bool enabled;
        bool bckd;
    };

    bool ShouldRegister() override;
    void OnReady() override;

    void Attach(std::function<Inputs()> inputs, std::function<void(uint64_t)> before,
                std::function<void(uint64_t, bool)> after);

    uint32_t Sadiv() const { return sadiv_; }
    void     WriteSadiv(uint64_t now, uint32_t value);
    void     ResetSadiv(uint64_t now);
    void     Update(uint64_t now);
    void     Restart(uint64_t now);
    void     ResetLine();

    bool                  Running() const { return running_; }
    GuestCycleClock::Rate Rate() const { return rate_; }
    uint64_t              BitsAt(uint64_t now) const { return bits_.TicksAt(now); }
    uint64_t              CycleOfBit(uint64_t bit) const { return bits_.CycleOfTick(bit); }

    void Save(StateWriter& w);
    void Restore(StateReader& r);
    void PostRestore();

private:
    GuestCycleClock::Rate Selected() const;
    void                  Place(uint64_t now);
    void                  OnCpuRate();

    GuestCycleClock*        clock_  = nullptr;
    Pxa255ClockManager*     clocks_ = nullptr;
    Pxa255Gpio*             gpio_   = nullptr;
    Pxa255I2sBitClockInput* pad_    = nullptr;

    std::function<Inputs()>             inputs_ = [] { return Inputs{false, false}; };
    std::function<void(uint64_t)>       before_ = [](uint64_t) {};
    std::function<void(uint64_t, bool)> after_  = [](uint64_t, bool) {};

    RatedTickCount           bits_;
    RatedTickCount::Position held_;
    GuestCycleClock::Rate    rate_{0u, 1u};
    bool                     running_ = false;
    uint32_t                 sadiv_   = 0;
};

}
