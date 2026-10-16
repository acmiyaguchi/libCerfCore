#include "../../socs/sa11xx/sa11xx_gpio_clock_input.h"

#include <cerfcore/boards/board_context.h>
#include "pangolin_family_id.h"
#include <cerfcore/core/cerf_emulator.h>

#include <cstdint>

namespace cerfcore {

namespace {

/* smartbook_g138_ce4_1 wavedev.dll sub_23C1BBC: SSCR1 ECS, SSCR0 16-bit TI frames with SCR 0,
   GAFR bit 19; smartbook_g138_ce4_2 wavedev.dll dword_1B88090: the hardware rate, 44100. */
constexpr uint64_t kFrameHz      = 44100u;
constexpr uint64_t kWordsPerFrame = 2u;
constexpr uint64_t kBitsPerWord  = 16u;
constexpr uint64_t kScrDivider   = 2u;

/* SA-1110 Table 11-5 (printed 11-5): GPIO 19 "Clock input pin for SSP". */
constexpr uint32_t kSspClockPin = 19u;

class PangolinSspClock : public Sa11xxGpioClockInput {
public:
    using Sa11xxGpioClockInput::Sa11xxGpioClockInput;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::Pangolin;
    }

    bool Frequency(uint32_t pin, GuestCycleClock::Rate& hz) const override {
        if (pin != kSspClockPin) return false;
        hz = GuestCycleClock::Rate{kFrameHz * kWordsPerFrame * kBitsPerWord * kScrDivider, 1u};
        return true;
    }
};

}

REGISTER_SERVICE_AS(PangolinSspClock, Sa11xxGpioClockInput);

}
