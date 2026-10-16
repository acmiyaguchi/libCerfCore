#include <cerfcore/peripherals/peripheral_base.h>

#include "../peripheral_dispatcher.h"
#include <cerfcore/boards/board_context.h>
#include "../../boards/nec_cronos3e/nec_cronos3e_family_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>

#include <cstdint>

namespace {

constexpr uint32_t kBase = 0x15003880u;
constexpr uint32_t kSize = 0x6u;

constexpr uint32_t kLcdDutyEn    = 0x0u;
constexpr uint32_t kLcdFreq      = 0x2u;
constexpr uint32_t kLcdDuty      = 0x4u;
constexpr uint16_t kLcdEnMask    = 0x0001u;
constexpr uint16_t kFreqMask     = 0x00FFu;
constexpr uint16_t kFreqReadOnes = 0xFF00u;
constexpr uint16_t kDutyMask     = 0x003Fu;

class Vrc4172Pwm : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::NecCronos3e;
    }

    void OnReady() override {
        ApplyReset();
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind kind) {
            if (kind == ResetLineKind::Rtc) ApplyReset();
        });
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    uint16_t ReadHalf(uint32_t addr) override {
        switch (addr - kBase) {
            case kLcdDutyEn: return lcd_en_;
            case kLcdFreq:   return static_cast<uint16_t>(kFreqReadOnes | freq_);
            case kLcdDuty:   return duty_;
        }
        emu_.Get<Fatal>().Die("Vrc4172Pwm: unaligned 16-bit read at 0x%08X", addr);
    }

    void WriteHalf(uint32_t addr, uint16_t value) override {
        switch (addr - kBase) {
            case kLcdDutyEn: lcd_en_ = value & kLcdEnMask; break;
            case kLcdFreq:   freq_   = value & kFreqMask;  break;
            case kLcdDuty:   duty_   = value & kDutyMask;  break;
            default:
                emu_.Get<Fatal>().Die("Vrc4172Pwm: unaligned 16-bit write 0x%04X at 0x%08X",
                                      value, addr);
        }
        LOG(Lcd, "Vrc4172Pwm: LCDBAK enable %u, FREQD 0x%02X, DUTY 0x%02X\n", lcd_en_, freq_,
            duty_);
    }

    void SaveState(StateWriter& w) override {
        w.Write("lcd_en", lcd_en_);
        w.Write("freq", freq_);
        w.Write("duty", duty_);
    }

    void RestoreState(StateReader& r) override {
        r.Read("lcd_en", lcd_en_);
        r.Read("freq", freq_);
        r.Read("duty", duty_);
    }

private:
    void ApplyReset() {
        lcd_en_ = 0u;
        freq_   = kFreqMask;
        duty_   = 0u;
    }

    uint16_t lcd_en_ = 0u;
    uint16_t freq_   = 0u;
    uint16_t duty_   = 0u;
};

}

REGISTER_SERVICE(Vrc4172Pwm);
