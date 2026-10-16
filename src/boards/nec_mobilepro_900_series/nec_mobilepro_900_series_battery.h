#pragma once

#include <cerfcore/core/service.h>
#include "../../host/battery_widget.h"

#include <cstdint>

namespace cerfcore {

/* NEC P530 main-battery model + status-bar widget: drives the GPIO/PCO inputs
   battery.dll reads (wired in the .cpp) from the widget's charge/AC state. */
class NecMobilepro900SeriesBattery : public Service {
public:
    explicit NecMobilepro900SeriesBattery(CerfEmulator& e) : Service(e) {}

    bool ShouldRegister() override;
    void OnReady() override;

private:
    void DriveState();
    /* Invert battery.dll's voltage table (0x1C842AC) through its poly
       (sub_1C81F88): scaled = 0.867924528*value + offset, offset 6426.34 on AC
       (C5) / 6646.34 on battery (C6). Returns the 16-bit raw the PCO reports for
       the widget's fill %, with the charging bit (0x8000) set when on AC. */
    uint16_t MainBatteryRaw(int fill_percent, bool on_ac) const;

    BatteryWidget battery_;
};

}
