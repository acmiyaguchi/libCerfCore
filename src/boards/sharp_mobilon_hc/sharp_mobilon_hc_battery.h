#pragma once

#include <cerfcore/core/service.h>
#include "../../host/battery_widget.h"

/* Owns the host battery widget for the Sharp Mobilon HC-4100 and exposes its
   fill level to the UCB1200 board seam - the main-battery AD0/AD2 sample and the
   AD3 backup sample gwes.exe reads over SIB1: (sub_9F160 / sub_9F38C). */
class SharpMobilonHcBattery : public Service {
public:
    explicit SharpMobilonHcBattery(CerfEmulator& e) : Service(e) {}
    bool ShouldRegister() override;
    void OnReady() override;

    int  FillPercent() const { return battery_.FillPercent(); }  /* 0..100 */
    bool IsOnBattery() const { return battery_.IsOnBattery(); }

private:
    BatteryWidget battery_;
};
