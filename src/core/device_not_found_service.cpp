#include "device_not_found_service.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/log.h>
#include <cerfcore/core/cerf_paths.h>

#include "../boot/sec_flash.h"
#include <cerfcore/host/configuration_problem.h>
#include <cerfcore/platform/host_file.h>
#include "../socs/imx51/imx51_nand_store.h"

#include <string>

namespace cerfcore {

REGISTER_SERVICE(DeviceNotFoundService);

bool DeviceNotFoundService::ShouldRegister() { return !IsDevicePresent(); }

bool DeviceNotFoundService::IsDevicePresent() {
    const auto& cfg = emu_.Get<DeviceConfig>();
    if (!cfg.rom_primary.empty() &&
        host_platform::HostFileExists(ResolveDeviceFile(cfg.device_name, cfg.rom_primary)))
        return true;
    if (auto* sf = emu_.TryGet<SecFlash>(); sf && sf->IsPresent()) return true;
    return emu_.TryGet<Imx51NandStore>() != nullptr;
}

void DeviceNotFoundService::EnsureFound() {
    const auto& cfg = emu_.Get<DeviceConfig>();
    LOG(Caution, "device '%s' has no ROM on disk\n", cfg.device_name.c_str());

#if !CERF_DEV_MODE
    emu_.Get<ConfigurationProblem>().DeviceRomMissing(cfg.device_name);
#endif

    CerfFatalExit(CERF_FATAL_USER_ERROR);
}

}
