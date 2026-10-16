#include "sec_flash.h"

#include <cerfcore/boards/board_context.h>
#include "../boards/ford_sync_gen2/ford_sync_gen2_family_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/log.h>
#include <cerfcore/core/cerf_paths.h>
#include <cerfcore/platform/host_file.h>

#include <string>

REGISTER_SERVICE(SecFlash);

bool SecFlash::ShouldRegister() {
    if (emu_.Get<BoardContext>().GetBoardFamilyId() != BoardFamilyId::FordSyncGen2) return false;
    const auto& cfg = emu_.Get<DeviceConfig>();
    if (cfg.rom_primary.empty()) return false;
    return host_platform::HostFileExists(ResolveDeviceFile(cfg.device_name, cfg.rom_primary));
}

void SecFlash::OnReady() {
    const auto&       cfg  = emu_.Get<DeviceConfig>();
    const std::string path = ResolveDeviceFile(cfg.device_name, cfg.rom_primary);

    if (!mf_.Open(path)) {
        LOG(Caution, "SecFlash: failed to map %s\n", path.c_str());
        return;
    }
    if (!sec_.Open(mf_)) {
        LOG(Caution, "SecFlash: %s is not a valid .sec container\n", path.c_str());
        return;
    }
    LOG(Boot, "SecFlash: %s - %u chunks, %.1f MB de-chunked flash\n",
        path.c_str(), sec_.Header().chunk_count,
        double(FlashSize()) / 1024.0 / 1024.0);
}
