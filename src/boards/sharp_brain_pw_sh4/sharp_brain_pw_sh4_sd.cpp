#include "sharp_brain_pw_sh4_sd.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/cerf_paths.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/fatal.h>
#include "../../core/host_file_bytes.h"
#include <cerfcore/core/log.h>
#include <cerfcore/platform/host_file.h>
#include "sharp_brain_pw_sh4_family_id.h"

namespace cerfcore {

namespace {

constexpr uint32_t kSdSlotIndex = 1u;

}

bool SharpBrainPwSh4Sd::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::SharpBrainPwSh4;
}

void SharpBrainPwSh4Sd::OnReady() {
    OpenStore();
}

std::string SharpBrainPwSh4Sd::StorePath() const {
    const DeviceConfig& cfg = emu_.Get<DeviceConfig>();
    if (cfg.storage_sd.empty()) return "";
    return ResolveDeviceFile(cfg.device_name, cfg.storage_sd);
}

void SharpBrainPwSh4Sd::OpenStore() {
    path_ = StorePath();
    if (path_.empty() || !HostFileNonEmpty(path_)) {
        present_ = false;
        return;
    }
    host_platform::HostFile file;
    uint64_t bytes = 0u;
    if (!file.Open(path_, host_platform::FileMode::kReadExisting) || !file.Size(bytes)) {
        present_ = false;
        return;
    }
    file.Close();
    if (bytes % 512u != 0u) {
        emu_.Get<Fatal>().Die("SD card in slot %u: storage.sd %s holds %llu bytes",
                              kSdSlotIndex, path_.c_str(),
                              static_cast<unsigned long long>(bytes));
    }
    if (!store_.Open(path_, bytes, false)) {
        emu_.Get<Fatal>().Die("SD card in slot %u: storage.sd %s cannot be opened",
                              kSdSlotIndex, path_.c_str());
    }
    sectors_ = static_cast<uint32_t>(store_.SectorCount());
    present_ = true;
    LOG(Boot, "SD card in slot %u: storage.sd %s, %u sectors\n", kSdSlotIndex,
        path_.c_str(), sectors_);
}

void SharpBrainPwSh4Sd::ReadBlock(uint32_t sector, uint8_t* out) {
    if (!store_.ReadSectors(sector, 1u, out)) {
        emu_.Get<Fatal>().Die("SD card in slot %u: sector %u cannot be read from %s",
                              kSdSlotIndex, sector, path_.c_str());
    }
}

void SharpBrainPwSh4Sd::WriteBlock(uint32_t sector, const uint8_t* data) {
    if (!store_.WriteSectors(sector, 1u, data)) {
        emu_.Get<Fatal>().Die("SD card in slot %u: sector %u cannot be written to %s",
                              kSdSlotIndex, sector, path_.c_str());
    }
}

REGISTER_SERVICE(SharpBrainPwSh4Sd);

}
