#pragma once

#include "../rtc/bcd_calendar.h"

#include <array>
#include <cstdint>

class StateReader;
class StateWriter;

class Tps65950RtcCalendar : public BcdCalendar {
public:
    enum Reg : uint8_t { kSeconds, kMinutes, kHours, kDays, kMonths, kYears, kWeeks, kRegCount };

    static constexpr uint32_t kAlarmCount = 6u;

    static constexpr uint8_t kCrossMinute = 1u << 0;
    static constexpr uint8_t kCrossHour   = 1u << 1;
    static constexpr uint8_t kCrossDay    = 1u << 2;

    static constexpr uint8_t kPm = 0x80u;

    using Regs  = std::array<uint8_t, kRegCount>;
    using Alarm = std::array<uint8_t, kAlarmCount>;

    struct Time {
        uint8_t sec   = 0;
        uint8_t min   = 0;
        uint8_t hour  = 0;
        uint8_t day   = 1;
        uint8_t month = 1;
        uint8_t year  = 0;
        uint8_t week  = 0;

        template <typename F>
        static constexpr void Visit(Time& t, F& field) {
            field("rtc_sec", t.sec);
            field("rtc_min", t.min);
            field("rtc_hour", t.hour);
            field("rtc_day", t.day);
            field("rtc_month", t.month);
            field("rtc_year", t.year);
            field("rtc_week", t.week);
        }
    };

    Regs     Encode(bool mode12) const;
    bool     Write(Reg reg, uint8_t value, bool mode12);
    uint8_t  StepSecond();
    uint8_t  RoundToMinute();
    void     Advance(uint64_t secs);
    uint64_t SecondsToBoundary(uint8_t every) const;
    uint64_t SecondsToNextHour() const { return BcdCalendar::SecondsToNextHour(t_.min, t_.sec); }
    bool     AtHour() const { return t_.min == 0u && t_.sec == 0u; }
    bool     Matches(const Alarm& alarm, bool mode12) const;
    bool     SecondsToAlarm(const Alarm& alarm, bool mode12, uint64_t& secs) const;

    void Save(StateWriter& w);
    void Restore(StateReader& r);

private:
    static bool    DecodeHour(uint8_t value, bool mode12, uint32_t& hour);
    static uint8_t EncodeHour(uint32_t hour, bool mode12);
    static void    StepDate(Time& t);

    Time t_;
};
