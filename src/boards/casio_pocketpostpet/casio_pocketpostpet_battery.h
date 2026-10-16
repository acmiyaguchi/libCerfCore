#pragma once

#include <cerfcore/core/service.h>
#include "../../host/battery_widget.h"

namespace cerfcore {

class CasioPocketpostpetBattery : public Service {
public:
    explicit CasioPocketpostpetBattery(CerfEmulator& e) : Service(e) {}

    bool ShouldRegister() override;
    void OnReady() override;

    int  FillPercent() const { return battery_.FillPercent(); }
    bool IsOnBattery() const { return battery_.IsOnBattery(); }

private:
    void DrivePowerPins();

    BatteryWidget battery_;
};

}
