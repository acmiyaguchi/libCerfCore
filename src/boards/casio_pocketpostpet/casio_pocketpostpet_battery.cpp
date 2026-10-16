#include "casio_pocketpostpet_battery.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/host_widget_registry.h>
#include "../../socs/vr41xx/vr41xx_giu.h"
#include <cerfcore/boards/board_context.h>
#include "casio_pocketpostpet_family_id.h"

namespace cerfcore {

bool CasioPocketpostpetBattery::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::CasioPocketpostpet;
}

void CasioPocketpostpetBattery::OnReady() {
    emu_.Get<HostWidgetRegistry>().Register(&battery_);
    battery_.SetChangeHandler([this] { DrivePowerPins(); });
    DrivePowerPins();
}

/* nk.exe sub_9F0B6198 and sub_9F0B61DC gate boot on GIUPIODH D10 (GPIO26) reading high;
   sub_9F0B61DC and gwes.exe sub_ABEEC read D8 (GPIO24) as external-power present. GIUPIODH
   bit10 = GPIO26, bit8 = GPIO24 (VR4121 UM 19.2.4). */
void CasioPocketpostpetBattery::DrivePowerPins() {
    auto& giu = emu_.Get<Vr41xxGiu>();
    const bool on_ac = !battery_.IsOnBattery();
    giu.SetPinLevel(26, on_ac || battery_.FillPercent() > 0);
    giu.SetPinLevel(24, on_ac);
}

REGISTER_SERVICE(CasioPocketpostpetBattery);

}
