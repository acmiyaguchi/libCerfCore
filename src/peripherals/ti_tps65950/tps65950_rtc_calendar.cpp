#include "tps65950_rtc_calendar.h"

#include <cerfcore/state/state_stream.h>

namespace cerfcore {

namespace {

/* TPS65950 TRM SWCU050E page 121: seconds and minutes 0-59, hours 00-23 or 1-12 in AM/PM mode, days per month,
   weeks 0-6, months 01-12, years 00-99; Tables 3-32 to 3-44 (pages 138-141) lay out the digit fields. */
constexpr BcdCalendar::Field kSecF{0x7Fu, 0u, 59u};
constexpr BcdCalendar::Field kMinF{0x7Fu, 0u, 59u};
constexpr BcdCalendar::Field kHour24{0x3Fu, 0u, 23u};
constexpr BcdCalendar::Field kHour12{0x3Fu, 1u, 12u};
constexpr BcdCalendar::Field kDayF{0x3Fu, 1u, 31u};
constexpr BcdCalendar::Field kMonthF{0x1Fu, 1u, 12u};
constexpr BcdCalendar::Field kYearF{0xFFu, 0u, 99u};
constexpr BcdCalendar::Field kWeekF{0x07u, 0u, 6u};

constexpr uint64_t kSecondsPerDay = 86400u;
constexpr uint32_t kCycleDays     = 36525u;

uint8_t Bcd(uint32_t value) { return static_cast<uint8_t>(BcdCalendar::ToBcd(value)); }

}

bool Tps65950RtcCalendar::DecodeHour(uint8_t value, bool mode12, uint32_t& hour) {
    if (!mode12) return DecodeField(kHour24, value, hour);
    uint32_t h12 = 0;
    if (!DecodeField(kHour12, value, h12)) return false;
    hour = h12 % 12u + ((value & kPm) != 0u ? 12u : 0u);
    return true;
}

uint8_t Tps65950RtcCalendar::EncodeHour(uint32_t hour, bool mode12) {
    if (!mode12) return Bcd(hour);
    const uint32_t h12 = hour % 12u == 0u ? 12u : hour % 12u;
    return static_cast<uint8_t>((hour >= 12u ? kPm : 0u) | Bcd(h12));
}

Tps65950RtcCalendar::Regs Tps65950RtcCalendar::Encode(bool mode12) const {
    Regs r{};
    r[kSeconds] = Bcd(t_.sec);
    r[kMinutes] = Bcd(t_.min);
    r[kHours]   = EncodeHour(t_.hour, mode12);
    r[kDays]    = Bcd(t_.day);
    r[kMonths]  = Bcd(t_.month);
    r[kYears]   = Bcd(t_.year);
    r[kWeeks]   = t_.week;
    return r;
}

bool Tps65950RtcCalendar::Write(Reg reg, uint8_t value, bool mode12) {
    uint32_t v = 0;
    bool     ok = false;
    switch (reg) {
        case kSeconds: ok = DecodeField(kSecF, value, v); break;
        case kMinutes: ok = DecodeField(kMinF, value, v); break;
        case kHours:   ok = DecodeHour(value, mode12, v); break;
        case kDays:    ok = DecodeField(kDayF, value, v); break;
        case kMonths:  ok = DecodeField(kMonthF, value, v); break;
        case kYears:   ok = DecodeField(kYearF, value, v); break;
        case kWeeks:   ok = DecodeField(kWeekF, value, v); break;
        case kRegCount: break;
    }
    if (!ok) return false;
    uint8_t Time::* const field[kRegCount] = {&Time::sec, &Time::min, &Time::hour, &Time::day,
                                              &Time::month, &Time::year, &Time::week};
    t_.*field[reg] = static_cast<uint8_t>(v);
    return true;
}

void Tps65950RtcCalendar::StepDate(Time& t) {
    t.week         = static_cast<uint8_t>((t.week + 1u) % 7u);
    uint32_t day   = t.day;
    uint32_t month = t.month;
    uint32_t year  = t.year;
    NextDate(day, month, year);
    t.day   = static_cast<uint8_t>(day);
    t.month = static_cast<uint8_t>(month);
    t.year  = static_cast<uint8_t>(year);
}

void Tps65950RtcCalendar::Advance(uint64_t secs) {
    uint32_t       sec  = t_.sec;
    uint32_t       min  = t_.min;
    uint32_t       hour = t_.hour;
    const uint64_t days = AddSeconds(sec, min, hour, secs);
    t_.sec  = static_cast<uint8_t>(sec);
    t_.min  = static_cast<uint8_t>(min);
    t_.hour = static_cast<uint8_t>(hour);
    for (uint64_t d = 0; d < days; ++d) StepDate(t_);
}

uint8_t Tps65950RtcCalendar::StepSecond() {
    Advance(1u);
    uint8_t cross = 0;
    if (t_.sec != 0u) return cross;
    cross |= kCrossMinute;
    if (t_.min != 0u) return cross;
    cross |= kCrossHour;
    if (t_.hour == 0u) cross |= kCrossDay;
    return cross;
}

/* TPS65950 TRM SWCU050E page 122: ROUND_30S sets the time to the closest minute during the next second;
   10H 59M 45S becomes 11H 00M 00S and 10H 59M 29S becomes 10H 59M 00S. */
uint8_t Tps65950RtcCalendar::RoundToMinute() {
    if (t_.sec < 30u) {
        t_.sec = 0u;
        return 0u;
    }
    t_.sec = 59u;
    return StepSecond();
}

/* TPS65950 TRM SWCU050E Table 3-62 (page 146): EVERY 0 second, 1 minute, 2 hour, 3 day. */
uint64_t Tps65950RtcCalendar::SecondsToBoundary(uint8_t every) const {
    switch (every & 3u) {
        case 0u: return 1u;
        case 1u: return SecondsToNextMinute(t_.sec);
        case 2u: return SecondsToNextHour();
    }
    return SecondsToNextDay(t_.hour, t_.min, t_.sec);
}

/* TPS65950 TRM SWCU050E page 122: INT_ALARM when the TC ALARM registers, of the same number and configuration as
   the TC registers, are the same as the TC registers. */
bool Tps65950RtcCalendar::Matches(const Alarm& alarm, bool mode12) const {
    const Regs    now  = Encode(mode12);
    const uint8_t hour = mode12 ? alarm[kHours] : static_cast<uint8_t>(alarm[kHours] & ~kPm);
    return alarm[kSeconds] == now[kSeconds] && alarm[kMinutes] == now[kMinutes] && hour == now[kHours] &&
           alarm[kDays] == now[kDays] && alarm[kMonths] == now[kMonths] && alarm[kYears] == now[kYears];
}

bool Tps65950RtcCalendar::SecondsToAlarm(const Alarm& alarm, bool mode12, uint64_t& secs) const {
    uint32_t s = 0, m = 0, h = 0, d = 0, mo = 0, y = 0;
    if (!DecodeField(kSecF, alarm[kSeconds], s) || !DecodeField(kMinF, alarm[kMinutes], m) ||
        !DecodeHour(alarm[kHours], mode12, h) || !DecodeField(kDayF, alarm[kDays], d) ||
        !DecodeField(kMonthF, alarm[kMonths], mo) || !DecodeField(kYearF, alarm[kYears], y) ||
        d > DaysInMonth(mo, y)) {
        return false;
    }
    const int64_t target = static_cast<int64_t>(h * 3600u + m * 60u + s);
    const int64_t now    = static_cast<int64_t>(t_.hour * 3600u + t_.min * 60u + t_.sec);
    Time          t      = t_;
    for (uint32_t i = 0; i <= kCycleDays; ++i, StepDate(t)) {
        if (t.day != d || t.month != mo || t.year != y) continue;
        const int64_t delta = static_cast<int64_t>(i) * static_cast<int64_t>(kSecondsPerDay) + target - now;
        if (delta <= 0) continue;
        secs = static_cast<uint64_t>(delta);
        return true;
    }
    return false;
}

void Tps65950RtcCalendar::Save(StateWriter& w) {
    static_assert(StateVisitCoversAllBytes<Time>([](Time& t, StateFieldBytes& f) { Time::Visit(t, f); }),
                  "Tps65950RtcCalendar::Time::Visit must name every field of Time");
    StateWriteField field(w);
    Time::Visit(t_, field);
}

void Tps65950RtcCalendar::Restore(StateReader& r) {
    StateReadField field(r);
    Time::Visit(t_, field);
}

}
