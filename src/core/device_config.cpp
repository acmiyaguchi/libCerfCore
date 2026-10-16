#include <cerfcore/core/device_config.h>
#include <cerfcore/core/cerf_emulator.h>
#include "config_loader.h"

namespace cerfcore {

REGISTER_SERVICE(DeviceConfig);

void DeviceConfig::OnReady() {
    emu_.Get<ConfigLoader>().LoadInto(*this);
}

}
