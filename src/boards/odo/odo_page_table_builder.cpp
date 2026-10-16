#include "../page_table_builder.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/boards/board_context.h>
#include "odo_family_id.h"
#include <cerfcore/cpu/emulated_memory.h>

#include <cstdint>
#include <vector>

namespace cerfcore {

namespace {

constexpr uint32_t MB(uint32_t mb) { return mb * 0x100000u; }

enum class OatKind { Dram, Flash, Mmio };

struct OatEntry {
    uint32_t va_base;
    uint32_t pa_base;
    uint32_t size;
    OatKind  kind;
};

constexpr OatEntry kOat[] = {
    /* 1 */ { 0x8C000000u, 0x0C000000u, MB(64), OatKind::Dram  }, /* 64 MB DRAM */
    /* 2 */ { 0x80000000u, 0x00000000u, MB(64), OatKind::Flash }, /* 64 MB Flash / boot ROM */
    /* 3 */ { 0x90000000u, 0x10000000u, MB(64), OatKind::Mmio  },
    /* 4 */ { 0x84000000u, 0x04000000u, MB(64), OatKind::Mmio  }, /* 64 MB Housekeeping FPGA (LEDs, SMC ether, scratch) */
};

constexpr uint32_t kDramPaBase = 0x0C000000u;
constexpr uint32_t kDramSize   = MB(64);

constexpr uint32_t kInitStackTopPa = kDramPaBase + kDramSize;

class OdoPageTableBuilder : public PageTableBuilder {
public:
    using PageTableBuilder::PageTableBuilder;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::Odo;
    }

    uint32_t InitStackTopPa() const override { return kInitStackTopPa; }
    uint32_t VaToPa(uint32_t va) const override;
    std::vector<DramRegion>   CachedDramRegions()   const override;
    std::vector<BackedRegion> BackedMemoryRegions() const override;
    std::vector<DramRegion>   MappedVaSpans()       const override;
};

uint32_t OdoPageTableBuilder::VaToPa(uint32_t va) const {
    for (const auto& e : kOat) {
        if (va >= e.va_base && va < e.va_base + e.size) {
            return e.pa_base + (va - e.va_base);
        }
    }
    LOG(Caution, "OdoPageTableBuilder::VaToPa: VA 0x%08X is "
            "outside every OAT band\n", va);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

std::vector<DramRegion> OdoPageTableBuilder::CachedDramRegions() const {
    std::vector<DramRegion> regions;
    for (const auto& e : kOat) {
        if (e.kind == OatKind::Dram) {
            regions.push_back({ e.va_base, e.pa_base, e.size });
        }
    }
    return regions;
}

std::vector<BackedRegion>
OdoPageTableBuilder::BackedMemoryRegions() const {
    std::vector<BackedRegion> regions;
    for (const auto& e : kOat) {
        if (e.kind == OatKind::Mmio) continue;
        const MemoryKind kind =
            (e.kind == OatKind::Flash) ? MemoryKind::kFlash : MemoryKind::kRam;
        regions.push_back({ e.va_base, e.pa_base, e.size, kind });
    }
    return regions;
}

std::vector<DramRegion> OdoPageTableBuilder::MappedVaSpans() const {
    std::vector<DramRegion> regions;
    for (const auto& e : kOat) {
        regions.push_back({ e.va_base, e.pa_base, e.size });
    }
    return regions;
}

}  /* namespace */

REGISTER_SERVICE_AS(OdoPageTableBuilder, PageTableBuilder);

}
