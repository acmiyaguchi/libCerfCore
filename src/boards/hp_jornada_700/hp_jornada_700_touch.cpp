#define NOMINMAX

#include "hp_jornada_700_touch.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include "../../socs/sa11xx/sa11xx_gpio.h"
#include <cerfcore/state/state_stream.h>
#include <cerfcore/boards/board_context.h>
#include "hp_jornada_700_family_id.h"

namespace cerfcore {

namespace {

/* Virtual panel raw-ADC characteristic (10-bit, linear across the surface).
   touch.dll's affine matrix calibrates it, so the align-screen applet adapts
   to whatever extents are produced here. */
constexpr uint16_t kAdcXMin = 130, kAdcXMax = 900;
constexpr uint16_t kAdcYMin = 120, kAdcYMax = 900;
/* jornada720 touch.dll sub_FB16D0 (TouchPanelGetDeviceCaps) and sub_FB1750
   (TouchPanelSetMode). */
constexpr GuestCycleClock::Rate kLowRate{100u, 1u};
constexpr GuestCycleClock::Rate kHighRate{200u, 1u};
constexpr uint8_t               kLowRateArgument  = 8u;
constexpr uint8_t               kHighRateArgument = 3u;

constexpr GuestClockTouchInput::PanelRange kPanel{kAdcXMin, kAdcXMax, kAdcYMin, kAdcYMax};

}

REGISTER_SERVICE_AS(HpJornada700Touch, TouchInput);

bool HpJornada700Touch::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::HpJornada700;
}

void HpJornada700Touch::OnTouchReady() {
    DrivePenLine(false);
}

void HpJornada700Touch::DrivePenLine(bool pen_down) {
    /* GPIO9: low = pen down (touch.dll collects), high = pen up. */
    emu_.Get<Sa11xxGpio>().DriveInputPin(9, !pen_down);
}

void HpJornada700Touch::PulsePenLine() {
    /* HP doc §4.4: a new sample is signalled by pulsing GPIO9 (high then low).
       The rising edge latches the driver's armed GRER bit 9 -> IRQ; by the time
       its IST reads GPLR the line is low again -> it collects a fresh sample. */
    auto& gpio = emu_.Get<Sa11xxGpio>();
    gpio.DriveInputPin(9, true);
    gpio.DriveInputPin(9, false);
}

void HpJornada700Touch::CurrentAdc(uint16_t* adc_x, uint16_t* adc_y) const {
    *adc_x = adc_x_;
    *adc_y = adc_y_;
}

void HpJornada700Touch::TapRawAdc(uint16_t adc_x, uint16_t adc_y) {
    QueueHostPen(HostPen{kRawTap, adc_x, adc_y});
}

void HpJornada700Touch::ApplyHostPen(const HostPen& pen) {
    if (pen.kind == kPenUp) {
        if (!pen_down_) return;
        synthetic_pulses_ = 0;
        pen_down_ = false;
        StopSamples();
        DrivePenLine(false);
        return;
    }
    if (pen.kind == kRawTap) {
        synthetic_pulses_ = 4;
        adc_x_ = static_cast<uint16_t>(pen.x);
        adc_y_ = static_cast<uint16_t>(pen.y);
    } else {
        synthetic_pulses_ = 0;
        MapToPanel(pen.x, pen.y, kPanel, &adc_x_, &adc_y_);
    }
    DrivePenLine(true);
    if (pen_down_) return;
    pen_down_ = true;
    StartSamples(PulseRate());
}

GuestCycleClock::Rate HpJornada700Touch::PulseRate() const {
    return high_rate_ ? kHighRate : kLowRate;
}

void HpJornada700Touch::OnSample() {
    if (synthetic_pulses_ > 0) {
        if (--synthetic_pulses_ == 0) {
            pen_down_ = false;
            StopSamples();
            DrivePenLine(false);
            return;
        }
    } else {
        int x = 0;
        int y = 0;
        LatestPosition(&x, &y);
        MapToPanel(x, y, kPanel, &adc_x_, &adc_y_);
    }
    PulsePenLine();
}

void HpJornada700Touch::SetHighRate(bool high) {
    high_rate_ = high;
    SetSampleRate(PulseRate());
}

void HpJornada700Touch::ResetSampleRate() { SetHighRate(false); }

void HpJornada700Touch::SelectSampleRate(uint8_t mcu_argument) {
    if (mcu_argument == kHighRateArgument) {
        SetHighRate(true);
    } else if (mcu_argument == kLowRateArgument) {
        SetHighRate(false);
    } else {
        LOG(Caution, "HpJornada700Touch: MCU 0xA2 argument 0x%02X selects no known sample rate\n",
            mcu_argument);
    }
}

void HpJornada700Touch::SaveState(StateWriter& w) const {
    w.Write<uint8_t>("touch_high_rate", high_rate_ ? 1u : 0u);
    w.Write<uint8_t>("touch_pen_down", pen_down_ ? 1u : 0u);
    w.Write<uint16_t>("touch_adc_x", adc_x_);
    w.Write<uint16_t>("touch_adc_y", adc_y_);
}

void HpJornada700Touch::RestoreState(StateReader& r) {
    uint8_t high = 0;
    uint8_t down = 0;
    r.Read("touch_high_rate", high);
    r.Read("touch_pen_down", down);
    r.Read("touch_adc_x", adc_x_);
    r.Read("touch_adc_y", adc_y_);
    high_rate_        = high != 0u;
    pen_down_         = down != 0u;
    synthetic_pulses_ = 0;
    ClearOnRestore();
}

void HpJornada700Touch::PostRestore() {
    if (!pen_down_) return;
    pen_down_ = false;
    DrivePenLine(false);
}

}
