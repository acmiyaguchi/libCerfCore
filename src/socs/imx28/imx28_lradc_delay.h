#pragma once

#include "../../execution/guest_cycle_clock.h"
#include "../oscillator_ticks.h"

#include <cstdint>
#include <functional>

namespace cerfcore {

class CerfEmulator;
class StateReader;
class StateWriter;

/* MCIMX28RM Rev. 2 section 38.5.14 DELAY: "This counter operates on a 2KHz clock derived from
   crystal clock." */
class Imx28LradcDelay {
public:
    static constexpr uint32_t kChannels = 4u;

    explicit Imx28LradcDelay(CerfEmulator& emu) : ticks_(emu, false), emu_(emu) {}

    void Init(std::function<void(uint32_t)> on_expiry);
    void Reset();
    void Kick(uint32_t channel, uint32_t delay, uint32_t loops);
    void Cancel(uint32_t channel);
    void Rescale();
    void Rearm();

    void Save(StateWriter& w);
    void Restore(StateReader& r);

private:
    void OnEvent(uint32_t channel);

    OscillatorTicks                ticks_;
    CerfEmulator&                  emu_;
    GuestCycleClock*               clock_ = nullptr;
    std::function<void(uint32_t)>  on_expiry_;
    GuestCycleClock::Event*        event_[kChannels] = {};
    uint8_t                        armed_[kChannels] = {};
    uint32_t                       count_[kChannels] = {};
    uint32_t                       loops_[kChannels] = {};
    uint64_t                       target_[kChannels] = {};
};

}
