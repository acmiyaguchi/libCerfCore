#pragma once

#include "../../peripherals/touch/guest_clock_touch_input.h"

#include <cstdint>

namespace cerfcore {

class StateReader;
class StateWriter;

class HpJornada700Touch : public GuestClockTouchInput {
public:
    using GuestClockTouchInput::GuestClockTouchInput;

    bool ShouldRegister() override;

    void CurrentAdc(uint16_t* adc_x, uint16_t* adc_y) const;

    /* jornada720 touch.dll sub_FB1314: bezel zones at raw X 31..67. */
    void TapRawAdc(uint16_t adc_x, uint16_t adc_y);

    void SelectSampleRate(uint8_t mcu_argument);
    void ResetSampleRate();

    void SaveState(StateWriter& w) const;
    void RestoreState(StateReader& r);
    void PostRestore();

protected:
    void OnTouchReady() override;
    void ApplyHostPen(const HostPen& pen) override;
    void OnSample() override;

private:
    static constexpr uint8_t kRawTap = kFirstOwnerKind;

    void     SetHighRate(bool high);
    GuestCycleClock::Rate PulseRate() const;
    void     DrivePenLine(bool pen_down);
    void     PulsePenLine();

    bool     high_rate_        = false;
    bool     pen_down_         = false;
    int      synthetic_pulses_ = 0;
    uint16_t adc_x_            = 0;
    uint16_t adc_y_            = 0;
};

}
