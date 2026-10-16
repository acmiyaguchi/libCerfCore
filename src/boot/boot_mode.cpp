#include "boot_mode.h"

#include <cerfcore/core/cerf_emulator.h>
#include "../core/no_emulation_runtime_service.h"

namespace cerfcore {

void BootMode::OnReady() {
    emu_.Get<NoEmulationRuntimeService>().EnsureEmulationPrevented();
}

}
