#pragma once

#include <cerfcore/core/service.h>
#include "../../host/battery_widget.h"

/* Owns the host battery widget for the SIMpad SL4 and exposes its state to the
   UCB1300 codec, which converts FillPercent into the AD1 ADC sample gwes reads
   and IsOnBattery into the AD2 AC-line sample. */
class SiemensSimpadBattery : public Service {
public:
    explicit SiemensSimpadBattery(CerfEmulator& e) : Service(e) {}
    bool ShouldRegister() override;
    void OnReady() override;

    int  FillPercent() const { return battery_.FillPercent(); }  /* 0..100 */
    bool IsOnBattery() const { return battery_.IsOnBattery(); }

private:
    BatteryWidget battery_;
};
