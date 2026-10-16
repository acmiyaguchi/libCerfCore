#include "../page_table_builder.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/boards/board_context.h>
#include "hp_jornada_820_family_id.h"

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

/* OEMAddressTable from the 820 nk.exe (IDA 0x8000114C, (VA,PA,MB) triplets).
   The 820 populates only DRAM bank 0 (16 MB, ROMHDR ulRAMEnd=0x88FFF000); the
   three empty SA-1100 banks are left unbacked (Mmio), so any access into them
   faults. */
constexpr OatEntry kOat[] = {
    { 0x88000000u, 0xC0000000u, MB(16), OatKind::Dram  }, /* DRAM bank 0 (populated) */
    { 0x89000000u, 0xD0000000u, MB(16), OatKind::Mmio  }, /* DRAM bank 2 (unpopulated) */
    { 0x8A000000u, 0xD8000000u, MB(16), OatKind::Mmio  }, /* DRAM bank 3 (unpopulated) */
    { 0x8B000000u, 0xC8000000u, MB(16), OatKind::Mmio  }, /* DRAM bank 1 (unpopulated) */
    { 0x80000000u, 0x00000000u, MB(32), OatKind::Flash }, /* System Flash/ROM */
    { 0x82000000u, 0x12000000u, MB(32), OatKind::Mmio  }, /* static chip-select nCS2 */
    { 0x84000000u, 0x18000000u, MB( 4), OatKind::Mmio  }, /* static bank 3 */
    { 0x84800000u, 0x1A000000u, MB( 4), OatKind::Mmio  }, /* static bank 4 */
    { 0x84C00000u, 0x1B000000u, MB( 4), OatKind::Mmio  }, /* static bank 4 alias */
    { 0x85000000u, 0x80000000u, MB( 4), OatKind::Mmio  }, /* SA-1100 system control (PPC/UART/GPIO/OST) */
    { 0x85400000u, 0x90000000u, MB( 4), OatKind::Mmio  }, /* SA-1100 power manager / RTC / reset */
    { 0x85800000u, 0xA0000000u, MB( 4), OatKind::Mmio  }, /* SA-1100 memory controller */
    { 0x85C00000u, 0xB0000000u, MB( 4), OatKind::Mmio  }, /* SA-1100 LCD / DMA controller */
    { 0x8C000000u, 0xE0000000u, MB(16), OatKind::Mmio  }, /* SA-1100 zeros bank / cache-flush */
    { 0x90000000u, 0x20000000u, MB(32), OatKind::Mmio  }, /* PCMCIA socket window 0 */
    { 0x92000000u, 0x24000000u, MB(32), OatKind::Mmio  }, /* PCMCIA socket window 1 */
    { 0x94000000u, 0x28000000u, MB(32), OatKind::Mmio  }, /* PCMCIA socket window 2 */
    { 0x96000000u, 0x2C000000u, MB(32), OatKind::Mmio  }, /* PCMCIA socket window 3 */
    { 0x98000000u, 0x30000000u, MB(32), OatKind::Mmio  }, /* PCMCIA socket window 4 */
    { 0x9A000000u, 0x34000000u, MB(32), OatKind::Mmio  }, /* PCMCIA socket window 5 */
    { 0x9C000000u, 0x38000000u, MB(32), OatKind::Mmio  }, /* PCMCIA socket window 6 */
    { 0x9E000000u, 0x3C000000u, MB(32), OatKind::Mmio  }, /* PCMCIA socket window 7 */
};

constexpr uint32_t kDramPaBase     = 0xC0000000u;
constexpr uint32_t kDramSize       = MB(16);
constexpr uint32_t kInitStackTopPa = kDramPaBase + kDramSize;

class HpJornada820PageTableBuilder : public PageTableBuilder {
public:
    using PageTableBuilder::PageTableBuilder;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::HpJornada820;
    }

    uint32_t InitStackTopPa() const override { return kInitStackTopPa; }
    uint32_t VaToPa(uint32_t va) const override;
    std::vector<DramRegion>   CachedDramRegions()   const override;
    std::vector<BackedRegion> BackedMemoryRegions() const override;
    std::vector<DramRegion>   MappedVaSpans()       const override;
};

uint32_t HpJornada820PageTableBuilder::VaToPa(uint32_t va) const {
    for (const auto& e : kOat) {
        if (va >= e.va_base && va < e.va_base + e.size) {
            return e.pa_base + (va - e.va_base);
        }
    }
    LOG(Caution, "HpJornada820PageTableBuilder::VaToPa: VA 0x%08X outside "
            "every OAT band (nk.exe firmware table @ 0x8000114C)\n", va);
    CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
}

std::vector<DramRegion> HpJornada820PageTableBuilder::CachedDramRegions() const {
    std::vector<DramRegion> regions;
    for (const auto& e : kOat) {
        if (e.kind == OatKind::Dram) {
            regions.push_back({ e.va_base, e.pa_base, e.size });
        }
    }
    return regions;
}

std::vector<BackedRegion>
HpJornada820PageTableBuilder::BackedMemoryRegions() const {
    std::vector<BackedRegion> regions;
    for (const auto& e : kOat) {
        if (e.kind == OatKind::Mmio) continue;
        const MemoryKind kind =
            (e.kind == OatKind::Flash) ? MemoryKind::kFlash : MemoryKind::kRam;
        regions.push_back({ e.va_base, e.pa_base, e.size, kind });
    }
    return regions;
}

std::vector<DramRegion> HpJornada820PageTableBuilder::MappedVaSpans() const {
    std::vector<DramRegion> regions;
    for (const auto& e : kOat) {
        regions.push_back({ e.va_base, e.pa_base, e.size });
    }
    return regions;
}

}  /* namespace */

REGISTER_SERVICE_AS(HpJornada820PageTableBuilder, PageTableBuilder);

}
