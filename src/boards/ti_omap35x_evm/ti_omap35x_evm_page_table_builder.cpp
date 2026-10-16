#include "../page_table_builder.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/boards/board_context.h>
#include "ti_omap35x_evm_family_id.h"

#include <cstdint>
#include <vector>

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
    /*  1 */ { 0x84000000u, 0x80000000u, MB(128), OatKind::Dram  }, /* 128 MB SDRAM bank 0 (extension RAM, see above) */
    /*  2 */ { 0x8C000000u, 0xA0000000u, MB(128), OatKind::Dram  }, /* 128 MB SDRAM bank 1 (primary, NK lives here) */
    /*  3 */ { 0x94000000u, 0x0C000000u, MB(16),  OatKind::Flash }, /* 16 MB  CS0 - OneNAND/NAND flash */
    /*  4 */ { 0x95000000u, 0x15000000u, MB(16),  OatKind::Mmio  }, /* 16 MB  CS5 - LAN9115 Ethernet */
    /*  5 */ { 0x96000000u, 0x48000000u, MB(16),  OatKind::Mmio  }, /* 16 MB  L4 Core/Wakeup registers */
    /*  6 */ { 0x97000000u, 0x49000000u, MB(1),   OatKind::Mmio  }, /*  1 MB  L4 Peripheral */
    /*  7 */ { 0x97100000u, 0x68000000u, MB(16),  OatKind::Mmio  }, /* 16 MB  L3 registers */
    /*  8 */ { 0x98100000u, 0x6C000000u, MB(16),  OatKind::Mmio  }, /* 16 MB  SMS (SDRAM Memory Scheduler) */
    /*  9 */ { 0x99100000u, 0x6D000000u, MB(16),  OatKind::Mmio  }, /* 16 MB  SDRC (SDRAM Refresh Controller) */
    /* 10 */ { 0x9A100000u, 0x6E000000u, MB(16),  OatKind::Mmio  }, /* 16 MB  GPMC (Gen Purpose Memory Controller) */
    /* 11 */ { 0x9B100000u, 0x40200000u, MB(1),   OatKind::Dram  }, /*  1 MB  62 KB on-chip SRAM (1 MB band) */
    /* 12 */ { 0x9B200000u, 0x5C000000u, MB(16),  OatKind::Mmio  }, /* 16 MB  L3 interconnect */
};

constexpr uint32_t kBank1PaBase    = 0xA0000000u;
constexpr uint32_t kBank1Size      = MB(128);
constexpr uint32_t kInitStackTopPa = kBank1PaBase + kBank1Size;

class TiOmap35xEvmPageTableBuilder : public PageTableBuilder {
public:
    using PageTableBuilder::PageTableBuilder;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::TiOmap35xEvm;
    }

    uint32_t InitStackTopPa() const override { return kInitStackTopPa; }
    uint32_t VaToPa(uint32_t va) const override;
    std::vector<DramRegion>   CachedDramRegions()   const override;
    std::vector<BackedRegion> BackedMemoryRegions() const override;
    std::vector<DramRegion>   MappedVaSpans()       const override;
};

uint32_t TiOmap35xEvmPageTableBuilder::VaToPa(uint32_t va) const {
    for (const auto& e : kOat) {
        if (va >= e.va_base && va < e.va_base + e.size) {
            return e.pa_base + (va - e.va_base);
        }
    }
    LOG(Caution, "TiOmap35xEvmPageTableBuilder::VaToPa: VA 0x%08X is "
            "outside every OAT band\n", va);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

std::vector<DramRegion> TiOmap35xEvmPageTableBuilder::CachedDramRegions() const {
    std::vector<DramRegion> regions;
    for (const auto& e : kOat) {
        if (e.kind == OatKind::Dram) {
            regions.push_back({ e.va_base, e.pa_base, e.size });
        }
    }
    return regions;
}

std::vector<BackedRegion>
TiOmap35xEvmPageTableBuilder::BackedMemoryRegions() const {
    std::vector<BackedRegion> regions;
    for (const auto& e : kOat) {
        if (e.kind == OatKind::Mmio) continue;
        const MemoryKind kind =
            (e.kind == OatKind::Flash) ? MemoryKind::kFlash : MemoryKind::kRam;
        regions.push_back({ e.va_base, e.pa_base, e.size, kind });
    }
    return regions;
}

std::vector<DramRegion> TiOmap35xEvmPageTableBuilder::MappedVaSpans() const {
    std::vector<DramRegion> regions;
    for (const auto& e : kOat) {
        regions.push_back({ e.va_base, e.pa_base, e.size });
    }
    return regions;
}

}  /* namespace */

REGISTER_SERVICE_AS(TiOmap35xEvmPageTableBuilder, PageTableBuilder);
