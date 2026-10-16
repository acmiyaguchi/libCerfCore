#pragma once

#include <cerfcore/core/service.h>
#include "../../host/battery_widget.h"

namespace cerfcore {

/* Holds the Jornada's battery charge model + status-bar widget. The MCU's
   GetBatteryData handler reads FillPercent() to synthesize the main-battery
   ADC the CE battery driver expects. */
class HpJornada700Battery : public Service {
public:
    explicit HpJornada700Battery(CerfEmulator& e) : Service(e) {}

    bool ShouldRegister() override;
    void OnReady() override;

    int FillPercent() const { return battery_.FillPercent(); }

private:
    /* battdrv.dll reads ACLineStatus from GPLR bit 4 (sub_ED18D8) and the
       charging flag from GPLR bit 26 (sub_ED1B4C 0xED1BE8); both pins are high
       when on battery. Push the widget's on_battery state onto them. */
    void DriveAcPins();

    BatteryWidget battery_;
};

}
