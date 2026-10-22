#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/host/host_widget_registry.h>
#include "../../host/battery_widget.h"
#include "../../socs/imx28/imx28_lradc_inputs.h"
#include "sharp_brain_pw_sh4_family_id.h"

#include <cstdint>

namespace cerfcore {

namespace {

/* sharp_brain_pw_sh4 battdrvr.dll BAT_UpdateStatus c0661c9c: BatteryLifePercent steps 100, 75,
   50, 25 start at 3901, 3751, 3451, 3351 mV; pmuPdk.dll 0x1022 (4130 mV) bounds 100; battdrvr.dll
   BAT_LowBatteryShutdown fatal below 3250 mV. */
constexpr uint32_t kFullFillMv   = 4015u;
constexpr uint32_t kThreeQuarter = 3825u;
constexpr uint32_t kHalfFillMv   = 3600u;
constexpr uint32_t kQuarterMv    = 3400u;
constexpr uint32_t kLowFillMv    = 3300u;

constexpr uint32_t kVdd5vMv = 5000u;

/* sharp_brain_pw_sh4 lradc.dll LDC_ReadBattTemp table 0xC06716B8: raw 1895 is 25 degC; section
   38.5.3 TEMP_ISRC0 0x7 is 140 uA; ROM LDC_ReadBattTemp programs ISRC0 7 with IENABLE0. */
constexpr uint32_t kThermistorMv   = 856u;
constexpr uint32_t kThermistorChan = 0u;
constexpr uint32_t kCtrl2Ienable0  = 1u << 8;
constexpr uint32_t kCtrl2Isrc0Mask = 0xFu;
constexpr uint32_t kCtrl2Isrc0Val  = 0x7u;

class SharpBrainPwSh4Battery : public Imx28LradcInputs {
public:
    using Imx28LradcInputs::Imx28LradcInputs;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SharpBrainPwSh4;
    }

    void OnReady() override {
        battery_.SetInitialState(true, 100);
        battery_.SetChangeHandler([this] { NotifyChanged(); });
        emu_.Get<HostWidgetRegistry>().Register(&battery_);
    }

    uint32_t PinMillivolts(uint32_t pin, uint32_t ctrl0, uint32_t ctrl2) override {
        const bool source_on = (ctrl2 & kCtrl2Ienable0) != 0u &&
                               (ctrl2 & kCtrl2Isrc0Mask) == kCtrl2Isrc0Val;
        if (pin == kThermistorChan && source_on) return kThermistorMv;
        emu_.Get<Fatal>().Die("SharpBrainPwSh4Battery: pin %u conversion with CTRL0 %08X CTRL2 %08X",
                              pin, ctrl0, ctrl2);
    }

    uint32_t BatteryMillivolts() override {
        const int fill = battery_.FillPercent();
        if (fill >= 100) return kFullFillMv;
        if (fill >= 75)  return kThreeQuarter;
        if (fill >= 50)  return kHalfFillMv;
        if (fill >= 25)  return kQuarterMv;
        return kLowFillMv;
    }

    uint32_t Vdd5vMillivolts() override { return battery_.IsOnBattery() ? 0u : kVdd5vMv; }

    bool TouchDetected(uint32_t) override { return false; }
    bool ButtonDetected(uint32_t, uint32_t) override { return false; }

private:
    BatteryWidget battery_;
};

}  /* namespace */

REGISTER_SERVICE_AS(SharpBrainPwSh4Battery, Imx28LradcInputs);

}
