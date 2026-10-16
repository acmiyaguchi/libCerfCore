#include "../page_table_builder.h"

#include "../../socs/msm8255/msm8255_ram_partitions.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/boards/board_context.h>
#include "nokia_sea_ray_family_id.h"

#include <cstdint>
#include <vector>

namespace {

constexpr uint32_t MB(uint32_t mb) { return mb * 0x100000u; }

struct OatEntry {
    uint32_t va_base;
    uint32_t pa_base;
    uint32_t size;
};

constexpr OatEntry kOat[] = {
    { 0x80000000u, 0x00200000u, MB(60) },
    { 0x83C00000u, 0x00000000u, MB(1)  },
};

/* Linux arch/arm/mach-msm msm_iomap-7x30.h:
   MSM_SHARED_RAM_PHYS 0x00100000, MSM_SHARED_RAM_SIZE SZ_1M. */
constexpr uint32_t kSharedRamPa   = 0x00100000u;
constexpr uint32_t kSharedRamSize = MB(1);

constexpr uint32_t DramTopPa() {
    uint32_t top = 0;
    for (const auto& e : kOat) {
        if (e.pa_base + e.size > top) top = e.pa_base + e.size;
    }
    return top;
}

class NokiaSeaRayPageTableBuilder : public PageTableBuilder {
public:
    using PageTableBuilder::PageTableBuilder;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::NokiaSeaRay;
    }

    uint32_t VaToPa(uint32_t va) const override;
    uint32_t InitStackTopPa() const override { return DramTopPa(); }
    std::vector<DramRegion>   CachedDramRegions()   const override;
    std::vector<BackedRegion> BackedMemoryRegions() const override;
    std::vector<DramRegion>   MappedVaSpans()       const override;
};

uint32_t NokiaSeaRayPageTableBuilder::VaToPa(uint32_t va) const {
    for (const auto& e : kOat) {
        if (va >= e.va_base && va - e.va_base < e.size) {
            return e.pa_base + (va - e.va_base);
        }
    }
    emu_.Get<Fatal>().Die("Lumia800: VA %08X is outside the OEMAddressTable", va);
}

std::vector<DramRegion>
NokiaSeaRayPageTableBuilder::CachedDramRegions() const {
    std::vector<DramRegion> regions;
    for (const auto& e : kOat) regions.push_back({ e.va_base, e.pa_base, e.size });
    return regions;
}

std::vector<BackedRegion>
NokiaSeaRayPageTableBuilder::BackedMemoryRegions() const {
    std::vector<BackedRegion> regions;
    for (const auto& e : kOat) {
        regions.push_back({ e.va_base, e.pa_base, e.size, MemoryKind::kRam });
    }
    regions.push_back({ kSharedRamPa, kSharedRamPa, kSharedRamSize,
                        MemoryKind::kRam });

    auto& parts = emu_.Get<Msm8255RamPartitions>();
    const uint32_t count = parts.PartitionCount();
    for (uint32_t i = 0; i < count; ++i) {
        const Msm8255RamPartition part = parts.Partition(i);
        regions.push_back({ part.start, part.start, part.size,
                            MemoryKind::kRam });
    }
    return regions;
}

std::vector<DramRegion>
NokiaSeaRayPageTableBuilder::MappedVaSpans() const {
    std::vector<DramRegion> spans;
    for (const auto& e : kOat) spans.push_back({ e.va_base, e.pa_base, e.size });
    return spans;
}

}  /* namespace */

REGISTER_SERVICE_AS(NokiaSeaRayPageTableBuilder, PageTableBuilder);
