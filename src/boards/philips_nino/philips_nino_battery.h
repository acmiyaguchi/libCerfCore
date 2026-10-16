#pragma once

#include <cerfcore/core/service.h>
#include "../../host/battery_widget.h"

namespace cerfcore {

/* Owns the host battery widget for the Philips Nino 300 and exposes its state to
   the UCB1200 board seam (the main-battery AD0 sample gwes reads) and the IO[5]
   AC-line pin. */
class PhilipsNinoBattery : public Service {
public:
    explicit PhilipsNinoBattery(CerfEmulator& e) : Service(e) {}
    bool ShouldRegister() override;
    void OnReady() override;

    int  FillPercent() const { return battery_.FillPercent(); }  /* 0..100 */
    bool IsOnBattery() const { return battery_.IsOnBattery(); }

private:
    BatteryWidget battery_;
};

}
