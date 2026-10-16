#include "philips_velo_1_battery.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/host_widget_registry.h>
#include <cerfcore/boards/board_context.h>
#include "philips_velo_1_family_id.h"

bool PhilipsVelo1Battery::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::PhilipsVelo1;
}

void PhilipsVelo1Battery::OnReady() {
    emu_.Get<HostWidgetRegistry>().Register(&battery_);
}

REGISTER_SERVICE(PhilipsVelo1Battery);
