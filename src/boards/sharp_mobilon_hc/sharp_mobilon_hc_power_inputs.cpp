#include "../../socs/pr31x00/pr31x00_power_inputs.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "sharp_mobilon_hc_family_id.h"
#include "sharp_mobilon_hc_battery.h"

namespace cerfcore {

namespace {

class SharpMobilonHcPowerInputs : public Pr31x00PowerInputs {
public:
    using Pr31x00PowerInputs::Pr31x00PowerInputs;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SharpMobilonHc;
    }

    bool PwrIntAsserted() const override {
        return !emu_.Get<SharpMobilonHcBattery>().IsOnBattery();
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(SharpMobilonHcPowerInputs, Pr31x00PowerInputs);

}
