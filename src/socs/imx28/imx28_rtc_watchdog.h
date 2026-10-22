#pragma once

#include "../../execution/guest_cycle_clock.h"
#include "../oscillator_ticks.h"
#include "imx28_sct_reg.h"

#include <cstdint>
#include <functional>

namespace cerfcore {

class CerfEmulator;
class StateReader;
class StateWriter;

/* MCIMX28RM Rev. 2 section 22.7: the RTC watchdog, counting the 1 kHz pulse of the owning
   block's millisecond oscillator. */
class Imx28RtcWatchdog {
public:
    Imx28RtcWatchdog(CerfEmulator& emu, StoppableOscillatorTicks& ms) : emu_(emu), ms_(ms) {}

    void Init(std::function<void()> on_event);
    void Reset();
    void Arm(bool run);
    void Disarm();

    uint32_t Value(bool enabled);
    void     Rebase(bool enabled);
    void     Write(Imx28SctOp op, uint32_t value, bool enabled);
    void     WriteDebug(Imx28SctOp op, uint32_t value);
    uint32_t DebugBits(bool enabled);
    void     OnExpiry();

    void Save(StateWriter& w);
    void Restore(StateReader& r);

private:
    CerfEmulator&            emu_;
    StoppableOscillatorTicks& ms_;
    GuestCycleClock*         clock_ = nullptr;
    GuestCycleClock::Event*  event_ = nullptr;

    uint32_t value_ = 0u;
    uint64_t anchor_ = 0u;
    bool     reset_mask_ = false;
};

}
