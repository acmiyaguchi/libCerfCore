#pragma once

#include <cerfcore/core/service.h>
#include "../../host/battery_widget.h"

namespace cerfcore {

class PhilipsVelo1Battery : public Service {
public:
    explicit PhilipsVelo1Battery(CerfEmulator& e) : Service(e) {}
    bool ShouldRegister() override;
    void OnReady() override;

    int FillPercent() const { return battery_.FillPercent(); }  /* 0..100 */

private:
    BatteryWidget battery_;
};

}
