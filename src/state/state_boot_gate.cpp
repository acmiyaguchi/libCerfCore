#include "state_boot_gate.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/execution/jit_runner.h>
#include <cerfcore/state/hibernation.h>

REGISTER_SERVICE(StateBootGate);

void StateBootGate::OnReady() {
    emu_.Get<JitRunner>().RegisterPreStartHook([this] { Run(); });
}

void StateBootGate::Run() {
    auto& hib = emu_.Get<Hibernation>();
    if (!hib.DefaultStateExists()) return;   /* nothing to resume -> cold boot */

    switch (emu_.Get<DeviceConfig>().boot_mode) {
        case StateBootMode::Cold:   break;
        case StateBootMode::Warm:   hib.Restore("", true);  break;
        case StateBootMode::Resume: hib.Restore("", false); break;
    }
}
