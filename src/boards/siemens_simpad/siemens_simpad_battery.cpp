#include "siemens_simpad_battery.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/host_widget_registry.h>
#include <cerfcore/boards/board_context.h>
#include "siemens_simpad_family_id.h"

namespace cerfcore {

bool SiemensSimpadBattery::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::SiemensSimpad;
}

void SiemensSimpadBattery::OnReady() {
    emu_.Get<HostWidgetRegistry>().Register(&battery_);
}

REGISTER_SERVICE(SiemensSimpadBattery);

}
