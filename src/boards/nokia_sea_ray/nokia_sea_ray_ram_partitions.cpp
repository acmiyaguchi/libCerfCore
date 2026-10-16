#include "../../socs/msm8255/msm8255_ram_partitions.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/boards/board_context.h>
#include "nokia_sea_ray_family_id.h"

#include <cstdint>

namespace {

constexpr uint32_t MB(uint32_t mb) { return mb * 0x100000u; }

constexpr uint32_t kCarveOutPa    = 0x03E00000u;
constexpr uint32_t kCarveOutBytes = MB(81);

constexpr uint32_t kAttrReadWrite = 1u;
constexpr uint32_t kCategoryEbi0Cs0 = 6u;
constexpr uint32_t kDomainApps     = 1u;
constexpr uint32_t kTypeAppsMemory = 5u;

constexpr uint32_t kPartitionCount = 1u;

class NokiaSeaRayRamPartitions : public Msm8255RamPartitions {
public:
    using Msm8255RamPartitions::Msm8255RamPartitions;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::NokiaSeaRay;
    }

    uint32_t PartitionCount() const override { return kPartitionCount; }

    Msm8255RamPartition Partition(uint32_t index) const override {
        if (index >= kPartitionCount) {
            emu_.Get<Fatal>().Die(
                "Lumia800: ram partition %u is outside the %u this board "
                "declares", index, kPartitionCount);
        }
        return {kCarveOutPa,   kCarveOutBytes, kAttrReadWrite,
                kCategoryEbi0Cs0, kDomainApps,  kTypeAppsMemory};
    }
};

}

REGISTER_SERVICE_AS(NokiaSeaRayRamPartitions, Msm8255RamPartitions);
