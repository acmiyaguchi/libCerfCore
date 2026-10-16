#pragma once

#include <cerfcore/core/service.h>
#include "../../execution/guest_cycle_clock.h"
#include "../../socs/oscillator_ticks.h"
#include "tps65950_rtc_calendar.h"

#include <cstdint>

namespace cerfcore {

class StateReader;
class StateWriter;
class Tps65950Interrupts;

class Tps65950Rtc : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    static bool Owns(uint8_t sub) { return sub >= kSeconds && sub <= kCompMsb; }

    uint8_t Read(uint8_t sub);
    void    Write(uint8_t sub, uint8_t value);

    void Save(StateWriter& w);
    void Restore(StateReader& r);
    void PostRestore();

private:
    using Cal = Tps65950RtcCalendar;

    static constexpr uint8_t kSeconds      = 0x1Cu;
    static constexpr uint8_t kAlarmSeconds = 0x23u;
    static constexpr uint8_t kCtrl         = 0x29u;
    static constexpr uint8_t kStatus       = 0x2Au;
    static constexpr uint8_t kInterrupts   = 0x2Bu;
    static constexpr uint8_t kCompLsb      = 0x2Cu;
    static constexpr uint8_t kCompMsb      = 0x2Du;

    bool     Running() const;
    bool     Mode12() const;
    bool     PerSecondWork() const;
    int64_t  CompValue() const;
    uint64_t SecondLength() const;
    Cal      AtNextBoundary(uint8_t& cross) const;
    uint64_t SecondStart(uint64_t n) const;
    bool     PeriodicAt(uint8_t cross) const;
    uint64_t NextInterruptSecond() const;
    bool     RtcIt() const;

    void LoadPowerOn();
    void LatchTime();
    void Advance();
    void OnSecond(uint8_t cross);
    void Compare();
    void OnEvent();
    void Publish();
    void Arm();
    void WriteTime(uint8_t sub, uint8_t value);
    void WriteCtrl(uint8_t value);
    void ReadStatusClear(uint8_t& value);

    GuestCycleClock*         clock_      = nullptr;
    GuestCycleClock::Event*  event_      = nullptr;
    Tps65950Interrupts*      interrupts_ = nullptr;
    StoppableOscillatorTicks osc_{emu_, true};
    Cal                      cal_;
    Cal::Regs                latch_{};
    Cal::Alarm               alarm_{};

    uint8_t  ctrl_          = 0u;
    uint8_t  status_        = 0u;
    uint8_t  it_            = 0u;
    uint8_t  comp_[2]       = {0u, 0u};
    uint8_t  week_hi_       = 0u;
    uint64_t sec_start_     = 0u;
    uint64_t sec_len_       = 0u;
    bool     round_pending_ = false;
    bool     alarm_match_   = false;
    bool     rtc_it_        = false;
};

}
