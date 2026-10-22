#pragma once

#include "../oscillator_ticks.h"

#include <cstdint>

namespace cerfcore {

class CerfEmulator;
class GuestDeepSleep;
class StateReader;
class StateWriter;

/* MCIMX28RM Rev. 2 sections 11.3.3 and 22.6: the RTC power-up sources of a powered-down chip,
   the alarm event and AUTO_RESTART. */
class Imx28RtcPowerDown {
public:
    Imx28RtcPowerDown(CerfEmulator& emu, OscillatorTicks& sec) : emu_(emu), sec_(sec) {}

    void Init(GuestDeepSleep& sleep) { sleep_ = &sleep; }

    void     Enter(uint32_t persistent0, uint64_t next_alarm_tick);
    uint32_t Exit();
    bool     SourceDue();
    int64_t  DueNs();

    void Save(StateWriter& w);
    void Restore(StateReader& r);

private:
    CerfEmulator&    emu_;
    OscillatorTicks& sec_;
    GuestDeepSleep*  sleep_ = nullptr;

    bool     down_             = false;
    bool     auto_restart_     = false;
    bool     alarm_            = false;
    int64_t  auto_restart_due_ = 0;
    uint64_t alarm_tick_       = 0u;
};

}
