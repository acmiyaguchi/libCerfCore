#include "../../peripherals/sk_hynix_h26m52002ckr/sk_hynix_h26m52002ckr.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/cerf_paths.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/fatal.h>
#include "../../core/host_file_bytes.h"
#include <cerfcore/core/log.h>
#include <cerfcore/platform/host_file.h>
#include "../../storage/disk_image.h"
#include "sharp_brain_pw_sh4_family_id.h"

#include <cstdint>
#include <string>

namespace cerfcore {

namespace {

/* sharp_brain_pw_sh4 sdhc.dll SHC_Init c0782c1c: SDHC1 (Index 1) drives SSP0. */
constexpr uint32_t kEmmcSlotIndex = 0u;

constexpr uint64_t kMaxSectors = 0xFFFFFFFFu;

class SharpBrainPwSh4Emmc : public SkHynixH26m52002ckr {
public:
    using SkHynixH26m52002ckr::SkHynixH26m52002ckr;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        if (!bd || bd->GetBoardFamilyId() != BoardFamilyId::SharpBrainPwSh4) return false;
        return HostFileNonEmpty(StorePath());
    }

    uint32_t SlotIndex() const override { return kEmmcSlotIndex; }

    void OnReady() override {
        OpenStore();
        SkHynixH26m52002ckr::OnReady();
    }

protected:
    uint32_t SectorCount() const override { return sectors_; }

    void ReadBlock(uint32_t sector, uint8_t* out) override {
        if (!store_.ReadSectors(sector, 1u, out)) {
            emu_.Get<Fatal>().Die("eMMC card in slot %u: sector %u cannot be read from %s",
                                  kEmmcSlotIndex, sector, path_.c_str());
        }
    }

    void WriteBlock(uint32_t sector, const uint8_t* data) override {
        if (!store_.WriteSectors(sector, 1u, data)) {
            emu_.Get<Fatal>().Die("eMMC card in slot %u: sector %u cannot be written to %s",
                                  kEmmcSlotIndex, sector, path_.c_str());
        }
    }

private:
    std::string StorePath() const {
        const DeviceConfig& cfg = emu_.Get<DeviceConfig>();
        return ResolveDeviceFile(cfg.device_name, cfg.storage_emmc);
    }

    void OpenStore() {
        path_ = StorePath();
        host_platform::HostFile file;
        uint64_t bytes = 0u;
        if (!file.Open(path_, host_platform::FileMode::kReadExisting) || !file.Size(bytes)) {
            emu_.Get<Fatal>().Die("eMMC card in slot %u: storage.emmc %s cannot be sized",
                                  kEmmcSlotIndex, path_.c_str());
        }
        file.Close();
        if (bytes % cerf_mmc::kBlockBytes != 0u || bytes / cerf_mmc::kBlockBytes > kMaxSectors) {
            emu_.Get<Fatal>().Die("eMMC card in slot %u: storage.emmc %s holds %llu bytes",
                                  kEmmcSlotIndex, path_.c_str(),
                                  static_cast<unsigned long long>(bytes));
        }
        if (!store_.Open(path_, bytes, false)) {
            emu_.Get<Fatal>().Die("eMMC card in slot %u: storage.emmc %s cannot be opened",
                                  kEmmcSlotIndex, path_.c_str());
        }
        sectors_ = static_cast<uint32_t>(store_.SectorCount());
        LOG(Boot, "eMMC card in slot %u: storage.emmc %s, %u sectors\n", kEmmcSlotIndex,
            path_.c_str(), sectors_);
    }

    DiskImage   store_;
    std::string path_;
    uint32_t    sectors_ = 0u;
};

}  // namespace

REGISTER_SERVICE_AS(SharpBrainPwSh4Emmc, MmcCard);

}
