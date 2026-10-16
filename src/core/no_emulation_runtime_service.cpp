#include "no_emulation_runtime_service.h"

#include "board_not_found_service.h"
#include <cerfcore/core/cerf_emulator.h>
#include "device_not_found_service.h"
#include <cerfcore/core/log.h>

REGISTER_SERVICE(NoEmulationRuntimeService);

void NoEmulationRuntimeService::OnReady() {
    EnsureEmulationPrevented();
}

void NoEmulationRuntimeService::EnsureEmulationPrevented() {
    if (checked_) return;
    checked_ = true;

    if (auto* d = emu_.TryGet<DeviceNotFoundService>()) d->EnsureFound();
    if (auto* b = emu_.TryGet<BoardNotFoundService>())  b->EnsureFound();
}
