#include "../../peripherals/silicon_motion_sm501/siemens_simatic_mp377_sm501_internal.h"
#include "siemens_simatic_mp377_smi_bridge_c480.h"
#include <cerfcore/core/cerf_emulator.h>

namespace siemens_simatic_mp377 {

uint32_t SiemensSimaticMp377SmiBridgeC480::MmioBase() const {
    return kSmiBridgeBase;
}

uint32_t SiemensSimaticMp377SmiBridgeC480::MmioSize() const {
    return kSmiBridgeWindowBytes;
}

void SiemensSimaticMp377SmiBridgeC480::SaveState(StateWriter& w) {
    emu_.Get<SiemensSimaticMp377SmiBridge>().SaveState(w);
}

void SiemensSimaticMp377SmiBridgeC480::RestoreState(StateReader& r) {
    emu_.Get<SiemensSimaticMp377SmiBridge>().RestoreState(r);
}

void SiemensSimaticMp377SmiBridgeC480::PostRestore() {
    emu_.Get<SiemensSimaticMp377SmiBridge>().PostRestoreState();
}

REGISTER_SERVICE(SiemensSimaticMp377SmiBridgeC480);

} // namespace siemens_mp377
