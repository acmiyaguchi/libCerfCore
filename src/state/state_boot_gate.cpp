#include "state_boot_gate.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/execution/jit_runner.h>
#include <cerfcore/state/hibernation.h>

namespace cerfcore {

REGISTER_SERVICE(StateBootGate);

void StateBootGate::OnReady() {
    emu_.Get<JitRunner>().RegisterPreStartHook([this] { Run(); });
}

void StateBootGate::RegisterColdHandoff(std::function<void()> fn) {
    cold_handoffs_.push_back(std::move(fn));
}

void StateBootGate::Run() {
    auto& hib = emu_.Get<Hibernation>();
    bool peripherals_restored = false;
    if (hib.DefaultStateExists()) {
        switch (emu_.Get<DeviceConfig>().boot_mode) {
            case StateBootMode::Cold:   break;
            case StateBootMode::Warm:   hib.Restore("", true); break;
            case StateBootMode::Resume: peripherals_restored = hib.Restore("", false); break;
        }
    }
    if (!peripherals_restored)
        for (auto& fn : cold_handoffs_) fn();
}

}
