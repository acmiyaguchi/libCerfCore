#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include "../../peripherals/rtc/bcd_calendar.h"
#include "../../socs/imx28/imx28_rtc_seconds_seed.h"
#include "sharp_brain_pw_sh4_family_id.h"

namespace cerfcore {

namespace {

constexpr uint32_t kEpochYear      = 2000u;
constexpr uint32_t kSecondsPerDay  = 86400u;

/* sharp_brain_pw_sh4 nk.exe FUN_8020faf8 (8020faf8): divisible by 4, except by 100 unless by 400. */
bool IsLeapYear(uint32_t year) {
    return year % 4u == 0u && (year % 100u != 0u || year % 400u == 0u);
}

/* sharp_brain_pw_sh4 nk.exe month length table at 80205b98, used by FUN_8020fc18 (8020fc18). */
constexpr uint32_t kMonthDays[12] = {31u, 28u, 31u, 30u, 31u, 30u, 31u, 31u, 30u, 31u, 30u, 31u};

/* sharp_brain_pw_sh4 nk.exe OEMGetRealTime (8020fd70), FUN_8020fcac (8020fcac) and FUN_8020fc18
   (8020fc18): SECONDS = day number * 86400 + time of day, day 1 = 2000-01-01. */
class SharpBrainPwSh4RtcSecondsSeed : public Imx28RtcSecondsSeed {
public:
    using Imx28RtcSecondsSeed::Imx28RtcSecondsSeed;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SharpBrainPwSh4;
    }

    uint32_t SecondsAtHostNow() override {
        const BcdCalendar::HostDate h = BcdCalendar::HostNow();
        if (h.year < kEpochYear) {
            emu_.Get<Fatal>().Die("SharpBrainPwSh4RtcSecondsSeed: host year %u before %u", h.year,
                                  kEpochYear);
        }
        uint32_t day = h.date;
        for (uint32_t y = kEpochYear; y < h.year; ++y) day += IsLeapYear(y) ? 366u : 365u;
        for (uint32_t m = 1u; m < h.month; ++m) {
            day += kMonthDays[m - 1u] + (m == 2u && IsLeapYear(h.year) ? 1u : 0u);
        }
        return day * kSecondsPerDay + h.hour * 3600u + h.min * 60u + h.sec;
    }
};

}

REGISTER_SERVICE_AS(SharpBrainPwSh4RtcSecondsSeed, Imx28RtcSecondsSeed);

}
